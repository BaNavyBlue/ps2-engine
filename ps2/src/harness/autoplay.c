/* (build 100b) COMPUTER PLAYS: the game's host for the route language's
 * interpreter. See autoplay.h and docs/computer_plays.md.
 *
 * (build 179) The player is the C player (src/player/, compiled in), run
 * on the command thread itself: no Python, no child, no pipe. Its lines go
 * to the interpreter as the driver's did, the log's back to it in
 * process. PS2_COMPUTER_DRIVER=python starts the Python driver as before,
 * the reference the C player is held to (docs/c_player.md); everything
 * below about the driver's process is that path's.
 *
 * (build 115) On Windows too. Only finding, starting and stopping the
 * driver differ, one section an OS below: on POSIX its pipes are its file
 * descriptors 3 and 4; a Windows child has no descriptors past 2, so there
 * the pipes' handles are named on its command line. The command thread, the
 * handoff and the run's directory are one code (winpthreads on Windows). */
#define _POSIX_C_SOURCE 200809L

#include "autoplay.h"
#include "probe.h"

#include "../core/assetfs.h"
#include "../core/audio.h"
#include "../game/gamedata.h"
#include "../player/player.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <stdint.h>
#include <wchar.h>
#define make_one_dir(path) _mkdir(path)
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#define make_one_dir(path) mkdir(path, 0755)

extern char **environ;
#endif

#define DRIVER        "tools/computer_plays.py"
#define HOME_PATH     512       /* (build 123) the folders below */
#define DRIVER_ARGS   24
#define STOP_WAIT_MS  1000      /* for the driver's report, on a hand-back */

/* (build 123) The saves folder, and COMPUTER PLAYS's own under it: its
 * directory (report, story, checkpoints, last.json, driver.log) and the
 * game's slots while it plays (autoplay_set_home). */
static char g_home[HOME_PATH - 32] = "saves";   /* room for /computer/slots */
static char g_dir[HOME_PATH]   = "saves/computer";
static char g_slots[HOME_PATH] = "saves/computer/slots";

void autoplay_set_home(const char *saves)
{
    snprintf(g_home, sizeof g_home, "%s",
             (saves != NULL && saves[0] != '\0') ? saves : "saves");
    snprintf(g_dir, sizeof g_dir, "%s/computer", g_home);
    snprintf(g_slots, sizeof g_slots, "%s/computer/slots", g_home);
}

const char *autoplay_dir(void)
{
    return g_dir;
}

/* (build 125) The stand-alone game (assetfs.h's pack, `make dist`): the
 * driver's files -- HARNESS below -- are the pack's, written out under the
 * computer's folder the first time it plays and again only when the
 * program's pack differs (harness/pack_id.txt), and the driver is started
 * there (--home), every path it is given whole, as it works from there.
 * The tree's game gives it the tree's files, as ever. */
static const char *const HARNESS[] = {
    "tools/", "routes/", "src/", "assets/data/", "assets/levels/",
    "assets/script/"
};

static char g_driver[HOME_PATH * 2] = DRIVER;   /* the script started */
static char g_out[HOME_PATH * 2];               /* --out, whole when packed */
static char g_harness[HOME_PATH * 2];           /* --home; "" unpacked */
static char g_start[HOME_PATH * 2];             /* --start, whole a path */

static bool packed(void)
{
    return assetfs_pack_count() > 0;
}

/* `path` whole: as it is when it already is, else under the working
 * folder. */
static void whole_path(char *out, size_t n, const char *path)
{
    bool whole = path[0] == '/' || path[0] == '\\'
              || (path[0] != '\0' && path[1] == ':');
    char cwd[HOME_PATH];
    if (whole || getcwd(cwd, sizeof cwd) == NULL) {
        snprintf(out, n, "%s", path);
        return;
    }
    snprintf(out, n, "%s/%s", cwd, path);
}

/* Every folder above the file `path`, made. */
static void make_parents(const char *path)
{
    char buf[HOME_PATH * 4];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p != '\0'; p++) {
        if ((*p == '/' || *p == '\\') && !(p == buf + 2 && buf[1] == ':')) {
            char c = *p;
            *p = '\0';
            make_one_dir(buf);
            *p = c;
        }
    }
}

/* The pack's HARNESS files under `dir`, unless pack_id.txt there says they
 * are this program's already. False when one cannot be written. */
static bool write_harness(const char *dir)
{
    char stamp[HOME_PATH * 2 + 16];
    char want[16];
    char have[16] = "";
    snprintf(stamp, sizeof stamp, "%s/pack_id.txt", dir);
    snprintf(want, sizeof want, "%08X", (unsigned)assetfs_pack_id());
    FILE *f = fopen(stamp, "rb");
    if (f != NULL) {
        size_t k = fread(have, 1, sizeof have - 1, f);
        have[k] = '\0';
        fclose(f);
    }
    if (strcmp(have, want) == 0) {
        return true;
    }
    int written = 0;
    for (int i = 0; i < assetfs_pack_count(); i++) {
        const char *path;
        const u8   *data;
        size_t      size;
        if (!assetfs_pack_entry(i, &path, &data, &size)) {
            break;
        }
        bool wanted = false;
        for (size_t h = 0; h < sizeof HARNESS / sizeof HARNESS[0]; h++) {
            if (strncmp(path, HARNESS[h], strlen(HARNESS[h])) == 0) {
                wanted = true;
            }
        }
        if (!wanted) {
            continue;
        }
        char dest[HOME_PATH * 2 + 512];
        snprintf(dest, sizeof dest, "%s/%s", dir, path);
        make_parents(dest);
        FILE *out = fopen(dest, "wb");
        if (out == NULL || fwrite(data, 1, size, out) != size) {
            fprintf(stderr, "computer plays: cannot write %s\n", dest);
            if (out != NULL) {
                fclose(out);
            }
            return false;
        }
        fclose(out);
        written++;
    }
    f = fopen(stamp, "wb");
    if (f != NULL) {
        fputs(want, f);
        fclose(f);
    }
    fprintf(stderr, "computer plays: %d files written out to %s\n", written,
            dir);
    return true;
}

/* What the driver needs besides python3 (docs/computer_plays.md). */
static const char *const NEEDED[] = {
    DRIVER, "tools/play.py", "tools/story.py", "tools/playthrough.py",
    "routes/new_game.route", "assets/data", "routes/saves"
};

/* (build 179) What the C player reads besides the game's own files: the
 * data its plans are made from (assets/data), the story's checkpoints, to
 * tell a player's save's stage (routes/saves), and the script names its
 * story log prints (src/game/script_ids.h) -- the tree's, or the pack's. */
static const char *const PLAYER_NEEDED[] = {
    "assets/data/items.json", "routes/saves/story_paseo.json",
    "src/game/script_ids.h"
};

#define PLAYER_STACK (16u << 20)    /* the C player's thread */

/* (build 179) PS2_COMPUTER_DRIVER=python: the Python driver, the
 * reference; the C player otherwise. */
static bool use_python(void)
{
    const char *d = getenv("PS2_COMPUTER_DRIVER");
    return d != NULL && strcmp(d, "python") == 0;
}

/* (build 179) PS2_COMPUTER_LOG=FILE: every log line the interpreter gives
 * either player, as route_probe's log.txt has them (a check's,
 * tools/computer_check.py). And the C player's own story lines, which the
 * Python printed to driver.log. */
static FILE *g_trace;
static FILE *g_echo;

#ifndef _WIN32

/* ---- the driver's process: POSIX ---- */

#define NO_PYTHON "no python3 on the PATH"

static bool on_path(const char *prog)
{
    const char *path = getenv("PATH");
    if (path == NULL) {
        return false;
    }
    char dir[1024];
    while (*path != '\0') {
        size_t n = strcspn(path, ":");
        if (n > 0 && n < sizeof dir - 64) {
            memcpy(dir, path, n);
            snprintf(dir + n, sizeof dir - n, "/%s", prog);
            if (access(dir, X_OK) == 0) {
                return true;
            }
        }
        path += n;
        if (*path == ':') {
            path++;
        }
    }
    return false;
}

static bool python_found(void)
{
    return on_path("python3");
}

static int high_fd(int fd)
{
    int moved = fcntl(fd, F_DUPFD, 10);
    close(fd);
    return moved;
}

/* python3 DRIVER, its commands on its fd 3 and the log on its 4: sets
 * a->child, a->cmd_fd and a->log_fd. */
static bool spawn_driver(Autoplay *a, const char *start, const char *extra)
{
    /* A write to a driver that has died must not take the game with it. */
    signal(SIGPIPE, SIG_IGN);

    int cmd[2], log[2];
    if (pipe(cmd) != 0) {
        return false;
    }
    if (pipe(log) != 0) {
        close(cmd[0]);
        close(cmd[1]);
        return false;
    }
    /* The child's ends go to 3 and 4 in the child; moved clear of both
     * first, so neither dup2 can land on the other's source. The game's
     * own ends are close-on-exec: a child holding the log pipe's write end
     * would never see it close. */
    int child_cmd = high_fd(cmd[1]);
    int child_log = high_fd(log[0]);
    fcntl(cmd[0], F_SETFD, FD_CLOEXEC);
    fcntl(log[1], F_SETFD, FD_CLOEXEC);
    /* Clear of 3 and 4 too, or the dup2 onto 4 would take it first. */
    char log_path[HOME_PATH + 16];
    snprintf(log_path, sizeof log_path, "%s/driver.log", g_dir);
    int out = open(log_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out >= 0) {
        out = high_fd(out);
    }

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, child_cmd, 3);
    posix_spawn_file_actions_adddup2(&fa, child_log, 4);
    posix_spawn_file_actions_addclose(&fa, child_cmd);
    posix_spawn_file_actions_addclose(&fa, child_log);
    if (out >= 0) {
        posix_spawn_file_actions_adddup2(&fa, out, 1);
        posix_spawn_file_actions_adddup2(&fa, out, 2);
    }

    /* python3 tools/computer_plays.py --attach 3,4 --start START [extra] */
    char  extra_buf[512];
    char *argv[DRIVER_ARGS + 1];
    int   argc = 0;
    argv[argc++] = "python3";
    argv[argc++] = g_driver;
    argv[argc++] = "--attach";
    argv[argc++] = "3,4";
    argv[argc++] = "--start";
    argv[argc++] = (char *)start;
    argv[argc++] = "--out";
    argv[argc++] = g_out;
    if (g_harness[0] != '\0') {             /* (build 125) */
        argv[argc++] = "--home";
        argv[argc++] = g_harness;
    }
    snprintf(extra_buf, sizeof extra_buf, "%s", (extra != NULL) ? extra : "");
    for (char *w = strtok(extra_buf, " \t"); w != NULL && argc < DRIVER_ARGS;
         w = strtok(NULL, " \t")) {
        argv[argc++] = w;
    }
    argv[argc] = NULL;

    /* The environment, with the hash seed playthrough.py pins (it would
     * restart itself to set it otherwise). */
    int envc = 0;
    while (environ[envc] != NULL) {
        envc++;
    }
    char **envp = calloc((size_t)envc + 2, sizeof *envp);
    int k = 0;
    for (int i = 0; i < envc; i++) {
        if (strncmp(environ[i], "PYTHONHASHSEED=", 15) != 0) {
            envp[k++] = environ[i];
        }
    }
    envp[k++] = "PYTHONHASHSEED=0";
    envp[k]   = NULL;

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, "python3", &fa, NULL, argv, envp);
    posix_spawn_file_actions_destroy(&fa);
    free(envp);
    close(child_cmd);
    close(child_log);
    if (out >= 0) {
        close(out);
    }
    if (rc != 0) {
        fprintf(stderr, "computer plays: cannot start python3 (%s)\n",
                strerror(rc));
        close(cmd[0]);
        close(log[1]);
        return false;
    }
    a->child  = pid;
    a->cmd_fd = cmd[0];
    a->log_fd = log[1];
    return true;
}

/* The driver given STOP_WAIT_MS to end by itself (its report), then
 * killed; reaped either way. */
static void reap_driver(Autoplay *a)
{
    int  status;
    bool reaped = false;
    for (int ms = 0; ms < STOP_WAIT_MS; ms += 10) {
        if (waitpid(a->child, &status, WNOHANG) == a->child) {
            reaped = true;
            break;
        }
        struct timespec t = { 0, 10 * 1000000L };
        nanosleep(&t, NULL);
    }
    if (!reaped) {
        kill(a->child, SIGKILL);
        waitpid(a->child, &status, 0);
    }
}

#else /* _WIN32 */

/* ---- (build 115) the driver's process: Windows ----
 * CreateProcess for posix_spawn. The driver's two pipe ends are inheritable
 * handles, named on its command line (`--attach CMD,LOG`, which
 * computer_plays.py opens with msvcrt.open_osfhandle), and only they and
 * its standard handles go down to it (PROC_THREAD_ATTRIBUTE_HANDLE_LIST):
 * no other child can hold a pipe open. The game's own two ends become C
 * descriptors (_open_osfhandle), which the command thread and host_log use
 * as on POSIX. */

#define NO_PYTHON   "no python3.exe, python.exe or py.exe on the PATH " \
                    "that is Python 3.8 or newer"
#define PIPE_BYTES  65536       /* Linux's pipe buffer; Windows's is 4 KB */
#define ASK_WAIT_MS 5000        /* for each Python found on the PATH */
#define PATH_CHARS  1024
#define LINE_CHARS  4096
#define ASKED_MAX   8

/* In this order, each looked for along the whole PATH before the next. */
static const wchar_t *const PYTHONS[] = {
    L"python3.exe", L"python.exe", L"py.exe"
};

/* The Pythons on the PATH asked whether they are one the driver runs on,
 * and their answers, for the session (find_python). */
static wchar_t g_asked_path[ASKED_MAX][PATH_CHARS];
static bool    g_asked_ok[ASKED_MAX];
static int     g_asked_n;

typedef struct {
    wchar_t text[LINE_CHARS];
    size_t  len;
    bool    full;               /* something did not fit: do not run it */
} CmdLine;

static void line_put(CmdLine *c, wchar_t ch, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (c->len + 1 >= LINE_CHARS) {
            c->full = true;
            return;
        }
        c->text[c->len++] = ch;
        c->text[c->len]   = L'\0';
    }
}

/* One argument onto a command line, quoted as the C runtime reads it back
 * (CommandLineToArgvW's rules): in quotes when it is empty or holds a
 * space, a tab or a quote; a quote inside escaped with a backslash; and the
 * backslashes before a quote, or before the closing one, doubled. */
static void line_arg(CmdLine *c, const wchar_t *arg)
{
    if (c->len > 0) {
        line_put(c, L' ', 1);
    }
    if (arg[0] != L'\0' && wcspbrk(arg, L" \t\"") == NULL) {
        for (const wchar_t *p = arg; *p != L'\0'; p++) {
            line_put(c, *p, 1);
        }
        return;
    }
    line_put(c, L'"', 1);
    const wchar_t *p = arg;
    for (;;) {
        size_t slashes = 0;
        while (*p == L'\\') {
            slashes++;
            p++;
        }
        if (*p == L'\0') {
            line_put(c, L'\\', slashes * 2);
            break;
        }
        line_put(c, L'\\', (*p == L'"') ? slashes * 2 + 1 : slashes);
        line_put(c, *p, 1);
        p++;
    }
    line_put(c, L'"', 1);
}

/* A narrow argument (the C runtime's code page: the start, the extras). */
static void line_arg_narrow(CmdLine *c, const char *arg)
{
    wchar_t w[PATH_CHARS];
    if (MultiByteToWideChar(CP_ACP, 0, arg, -1, w, PATH_CHARS) == 0) {
        c->full = true;
        return;
    }
    line_arg(c, w);
}

/* Whether `exe` runs a Python the driver runs on: (build 162) `exe -c`
 * with a check of its version, 3.8 or newer (py.exe with -3, which runs
 * the newest), ends with 0 within ASK_WAIT_MS -- no window, nothing
 * inherited. Until 162 this was `-c ""`, asked of reparse points alone,
 * so any Python went, however old; the driver needs 3.8 (Path.unlink's
 * missing_ok, the oldest it is run on). */
static bool python_answers(const wchar_t *exe, bool py)
{
    CmdLine c;
    memset(&c, 0, sizeof c);
    line_arg(&c, exe);
    if (py) {
        line_arg(&c, L"-3");
    }
    line_arg(&c, L"-c");
    line_arg(&c, L"import sys; sys.exit(sys.version_info < (3, 8))");
    if (c.full) {
        return false;
    }
    STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, c.text, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        return false;
    }
    CloseHandle(pi.hThread);
    DWORD code = 1;
    if (WaitForSingleObject(pi.hProcess, ASK_WAIT_MS) == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &code);
    } else {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    CloseHandle(pi.hProcess);
    return code == 0;
}

/* python_answers, asked once a session for each path. */
static bool python_asked(const wchar_t *exe, bool py)
{
    for (int i = 0; i < g_asked_n; i++) {
        if (wcscmp(g_asked_path[i], exe) == 0) {
            return g_asked_ok[i];
        }
    }
    bool ok = python_answers(exe, py);
    if (g_asked_n < ASKED_MAX) {
        wcscpy(g_asked_path[g_asked_n], exe);
        g_asked_ok[g_asked_n] = ok;
        g_asked_n++;
    }
    return ok;
}

/* (build 122) The Python `make dist` puts beside the game, python\python.exe
 * in the game's own folder, into `exe`: a player need install none. */
static bool bundled_python(wchar_t *exe, size_t n)
{
    static const wchar_t TAIL[] = L"\\python\\python.exe";

    DWORD len = GetModuleFileNameW(NULL, exe, (DWORD)n);
    if (len == 0 || len >= n) {
        return false;
    }
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash == NULL || (size_t)(slash - exe) + wcslen(TAIL) + 1 > n) {
        return false;
    }
    wcscpy(slash, TAIL);
    DWORD attr = GetFileAttributesW(exe);
    return attr != INVALID_FILE_ATTRIBUTES
        && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/* (build 122) The bundled Python first (bundled_python); else python3.exe,
 * python.exe or py.exe (then *py, run with -3), the first on
 * the ';' PATH, into `exe`, that answers as a Python 3.8 or newer
 * (python_asked): the Store's stand-ins in WindowsApps are app execution
 * aliases which, with no Python installed, only say where to get one, and
 * (build 162) an older Python is passed over for the next. */
static bool find_python(wchar_t *exe, size_t n, bool *py)
{
    *py = false;
    if (bundled_python(exe, n)) {
        return true;
    }
    DWORD need = GetEnvironmentVariableW(L"PATH", NULL, 0);
    if (need == 0) {
        return false;
    }
    wchar_t *path = malloc((size_t)need * sizeof *path);
    if (path == NULL || GetEnvironmentVariableW(L"PATH", path, need) == 0) {
        free(path);
        return false;
    }
    bool found = false;
    for (size_t k = 0; k < sizeof PYTHONS / sizeof PYTHONS[0] && !found; k++) {
        size_t name = wcslen(PYTHONS[k]);
        for (const wchar_t *e = path; *e != L'\0' && !found;) {
            size_t         len = wcscspn(e, L";");
            const wchar_t *dir = e;
            size_t         dl  = len;
            if (dl >= 2 && dir[0] == L'"' && dir[dl - 1] == L'"') {
                dir++;
                dl -= 2;
            }
            if (dl > 0 && dl + name + 2 < n) {
                wmemcpy(exe, dir, dl);
                if (exe[dl - 1] != L'\\' && exe[dl - 1] != L'/') {
                    exe[dl++] = L'\\';
                }
                wcscpy(exe + dl, PYTHONS[k]);
                DWORD attr = GetFileAttributesW(exe);
                if (attr != INVALID_FILE_ATTRIBUTES
                    && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                    *py   = wcscmp(PYTHONS[k], L"py.exe") == 0;
                    found = python_asked(exe, *py);
                }
            }
            e += len;
            if (*e == L';') {
                e++;
            }
        }
    }
    free(path);
    return found;
}

static bool python_found(void)
{
    wchar_t exe[PATH_CHARS];
    bool    py = false;
    return find_python(exe, PATH_CHARS, &py);
}

/* The game's environment with PYTHONHASHSEED=0 in place of any of its own,
 * at the place the block's order (by name, case folded) puts it: the hash
 * seed playthrough.py pins, which it would start itself again to set --
 * and a second process would not have the pipes. NULL if out of memory. */
static wchar_t *driver_env(void)
{
    static const wchar_t SEED[]      = L"PYTHONHASHSEED=0";
    static const wchar_t SEED_NAME[] = L"PYTHONHASHSEED";
    const int            seed_len    = (int)wcslen(SEED_NAME);

    wchar_t *env = GetEnvironmentStringsW();
    if (env == NULL) {
        return NULL;
    }
    size_t total = 0;
    for (const wchar_t *p = env; *p != L'\0'; p += wcslen(p) + 1) {
        total += wcslen(p) + 1;
    }
    wchar_t *out = malloc((total + wcslen(SEED) + 2) * sizeof *out);
    if (out == NULL) {
        FreeEnvironmentStringsW(env);
        return NULL;
    }
    size_t k      = 0;
    bool   placed = false;
    for (const wchar_t *p = env; *p != L'\0'; p += wcslen(p) + 1) {
        size_t         len = wcslen(p);
        const wchar_t *eq  = wcschr(p + 1, L'=');   /* "=C:=C:\..." too */
        int            nl  = (int)((eq != NULL) ? (size_t)(eq - p) : len);
        int            cmp = CompareStringOrdinal(p, nl, SEED_NAME,
                                                  seed_len, TRUE);
        if (cmp == CSTR_EQUAL) {
            continue;
        }
        if (!placed && cmp == CSTR_GREATER_THAN) {
            wmemcpy(out + k, SEED, wcslen(SEED) + 1);
            k += wcslen(SEED) + 1;
            placed = true;
        }
        wmemcpy(out + k, p, len + 1);
        k += len + 1;
    }
    if (!placed) {
        wmemcpy(out + k, SEED, wcslen(SEED) + 1);
        k += wcslen(SEED) + 1;
    }
    out[k] = L'\0';
    FreeEnvironmentStringsW(env);
    return out;
}

/* python DRIVER, its commands and log on the two pipes' handles its command
 * line names, its stdout and stderr to driver.log, its stdin nothing: sets
 * a->child, a->process, a->cmd_fd and a->log_fd. */
static bool spawn_driver(Autoplay *a, const char *start, const char *extra)
{
    wchar_t exe[PATH_CHARS];
    bool    py = false;
    if (!find_python(exe, PATH_CHARS, &py)) {
        fprintf(stderr, "computer plays: %s\n", NO_PYTHON);
        return false;
    }

    HANDLE cmd_rd, cmd_wr, log_rd, log_wr;
    if (!CreatePipe(&cmd_rd, &cmd_wr, NULL, PIPE_BYTES)) {
        return false;
    }
    if (!CreatePipe(&log_rd, &log_wr, NULL, PIPE_BYTES)) {
        CloseHandle(cmd_rd);
        CloseHandle(cmd_wr);
        return false;
    }
    /* CreatePipe with no attributes makes neither end inheritable: the
     * driver's two are made so; the game's stay its own, or a child holding
     * the log pipe's write end would never see it close. */
    SetHandleInformation(cmd_wr, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(log_rd, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    SECURITY_ATTRIBUTES inherit;
    memset(&inherit, 0, sizeof inherit);
    inherit.nLength        = sizeof inherit;
    inherit.bInheritHandle = TRUE;
    char log_path[HOME_PATH + 16];
    snprintf(log_path, sizeof log_path, "%s/driver.log", g_dir);
    HANDLE out = CreateFileA(log_path, GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             OPEN_EXISTING, 0, NULL);
    HANDLE std_in  = (nul != INVALID_HANDLE_VALUE) ? nul : NULL;
    HANDLE std_out = (out != INVALID_HANDLE_VALUE) ? out : std_in;
    if (out == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "computer plays: cannot write %s\n", log_path);
    }
    HANDLE handles[4];
    DWORD  nh = 0;
    handles[nh++] = cmd_wr;
    handles[nh++] = log_rd;
    if (std_in != NULL) {
        handles[nh++] = std_in;
    }
    if (std_out != NULL && std_out != std_in) {
        handles[nh++] = std_out;
    }

    /* python tools/computer_plays.py --attach CMD,LOG --start START
     * --out DIR [extra] (py.exe with -3 first) */
    CmdLine c;
    memset(&c, 0, sizeof c);
    line_arg(&c, exe);
    if (py) {
        line_arg(&c, L"-3");
    }
    line_arg_narrow(&c, g_driver);
    line_arg(&c, L"--attach");
    wchar_t spec[64];
    swprintf(spec, sizeof spec / sizeof spec[0], L"%llu,%llu",
             (unsigned long long)(uintptr_t)cmd_wr,
             (unsigned long long)(uintptr_t)log_rd);
    line_arg(&c, spec);
    line_arg(&c, L"--start");
    line_arg_narrow(&c, start);
    line_arg(&c, L"--out");
    line_arg_narrow(&c, g_out);
    int  argc = 8;              /* as many extras as POSIX's argv takes */
    if (g_harness[0] != '\0') {             /* (build 125) */
        line_arg(&c, L"--home");
        line_arg_narrow(&c, g_harness);
        argc += 2;
    }
    char extra_buf[512];
    snprintf(extra_buf, sizeof extra_buf, "%s", (extra != NULL) ? extra : "");
    for (char *w = strtok(extra_buf, " \t"); w != NULL && argc < DRIVER_ARGS;
         w = strtok(NULL, " \t")) {
        line_arg_narrow(&c, w);
        argc++;
    }

    wchar_t *envp  = driver_env();
    SIZE_T   size  = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &size);
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = malloc(size);
    bool listed = attrs != NULL
               && InitializeProcThreadAttributeList(attrs, 1, 0, &size);
    bool ok     = listed && envp != NULL && !c.full
               && UpdateProcThreadAttribute(attrs, 0,
                                            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                            handles, nh * sizeof handles[0],
                                            NULL, NULL);
    STARTUPINFOEXW si;
    memset(&si, 0, sizeof si);
    si.StartupInfo.cb         = sizeof si;
    si.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput  = std_in;
    si.StartupInfo.hStdOutput = std_out;
    si.StartupInfo.hStdError  = std_out;
    si.lpAttributeList        = attrs;
    PROCESS_INFORMATION pi;
    DWORD err = 0;
    if (ok) {
        ok = CreateProcessW(exe, c.text, NULL, NULL, TRUE,
                            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT
                            | EXTENDED_STARTUPINFO_PRESENT,
                            envp, NULL, &si.StartupInfo, &pi);
        err = ok ? 0 : GetLastError();
    }
    if (listed) {
        DeleteProcThreadAttributeList(attrs);
    }
    free(attrs);
    free(envp);
    CloseHandle(cmd_wr);
    CloseHandle(log_rd);
    if (out != INVALID_HANDLE_VALUE) {
        CloseHandle(out);
    }
    if (nul != INVALID_HANDLE_VALUE) {
        CloseHandle(nul);
    }
    if (!ok) {
        fprintf(stderr, "computer plays: cannot start %ls (error %lu)\n",
                exe, (unsigned long)err);
        CloseHandle(cmd_rd);
        CloseHandle(log_wr);
        return false;
    }
    CloseHandle(pi.hThread);
    fprintf(stderr, "computer plays: %ls\n", exe);     /* (build 122) */
    a->process = pi.hProcess;
    a->child   = (pid_t)pi.dwProcessId;
    a->cmd_fd  = _open_osfhandle((intptr_t)cmd_rd, _O_RDONLY | _O_BINARY);
    a->log_fd  = _open_osfhandle((intptr_t)log_wr, _O_WRONLY | _O_BINARY);
    if (a->cmd_fd < 0 || a->log_fd < 0) {
        if (a->cmd_fd >= 0) {
            close(a->cmd_fd);
        } else {
            CloseHandle(cmd_rd);
        }
        if (a->log_fd >= 0) {
            close(a->log_fd);
        } else {
            CloseHandle(log_wr);
        }
        a->cmd_fd = -1;
        a->log_fd = -1;
        TerminateProcess(a->process, 1);
        WaitForSingleObject(a->process, INFINITE);
        CloseHandle(a->process);
        a->process = NULL;
        a->child   = -1;
        return false;
    }
    return true;
}

/* The driver given STOP_WAIT_MS to end by itself (its report), then
 * terminated; its handle closed either way. */
static void reap_driver(Autoplay *a)
{
    if (WaitForSingleObject(a->process, STOP_WAIT_MS) != WAIT_OBJECT_0) {
        TerminateProcess(a->process, 1);
        WaitForSingleObject(a->process, INFINITE);
    }
    CloseHandle(a->process);
    a->process = NULL;
}

#endif /* _WIN32 */

/* ---- the lock, and waking the other thread ----
 * (build 115) On Windows, Windows's own SRW lock and condition variable,
 * not winpthreads': a winpthreads broadcast first waits until the thread it
 * woke last has run (its condition variable's gate), so each tick handed
 * between the frame loop and the command thread cost about 130 us here,
 * where these take 2, and FAST ran at a seventh of the probe's speed. And a
 * thread about to sleep spins first, for up to SPIN_US, watching the count
 * of wake-ups without the lock: a thread that has slept costs about 100 us
 * to wake in this VM, which every command's first tick paid. */

#ifdef _WIN32

#define SPIN_US 200

#define SRW(a) ((PSRWLOCK)&(a)->lock)
#define CV(a)  ((PCONDITION_VARIABLE)&(a)->cond)

_Static_assert(sizeof(SRWLOCK) == sizeof(void *)
               && sizeof(CONDITION_VARIABLE) == sizeof(void *),
               "autoplay.h keeps each in a pointer's room");

static void lock_init(Autoplay *a)
{
    InitializeSRWLock(SRW(a));
    InitializeConditionVariable(CV(a));
    a->wakes = 0;
}

static void lock_take(Autoplay *a)
{
    AcquireSRWLockExclusive(SRW(a));
}

static void lock_give(Autoplay *a)
{
    ReleaseSRWLockExclusive(SRW(a));
}

static long wakes_seen(Autoplay *a)
{
    return InterlockedCompareExchange(&a->wakes, 0, 0);
}

/* Always with the lock held, so a sleeper cannot miss one (sleep_wake). */
static void wake_all(Autoplay *a)
{
    InterlockedIncrement(&a->wakes);
    WakeAllConditionVariable(CV(a));
}

/* Holding the lock: let go of it until a wake_all, taken again after. The
 * first SPIN_US spent spinning on the count, the lock not held; then
 * asleep for `ms` at most (INFINITE for no end). False on a timeout. */
static bool sleep_wake(Autoplay *a, DWORD ms)
{
    static LONGLONG hz;
    if (hz == 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        hz = f.QuadPart;
    }
    long seen = wakes_seen(a);
    lock_give(a);
    LARGE_INTEGER t0, t;
    QueryPerformanceCounter(&t0);
    do {
        YieldProcessor();
        QueryPerformanceCounter(&t);
    } while (wakes_seen(a) == seen
             && (t.QuadPart - t0.QuadPart) * 1000000 < SPIN_US * hz);
    lock_take(a);
    if (wakes_seen(a) != seen) {
        return true;
    }
    if (!SleepConditionVariableSRW(CV(a), SRW(a), ms, 0)) {
        return GetLastError() != ERROR_TIMEOUT;
    }
    return true;
}

/* The lock let go until woken, then taken again. */
static void wait_wake(Autoplay *a)
{
    sleep_wake(a, INFINITE);
}

/* The same, or false once `until` (CLOCK_REALTIME) has passed. */
static bool wait_wake_until(Autoplay *a, const struct timespec *until)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    double left = (double)(until->tv_sec - now.tv_sec)
                + (double)(until->tv_nsec - now.tv_nsec) / 1e9;
    if (left <= 0.0) {
        return false;
    }
    return sleep_wake(a, (DWORD)(left * 1000.0 + 0.999));
}

#else

static void lock_init(Autoplay *a)
{
    pthread_mutex_init(&a->lock, NULL);
    pthread_cond_init(&a->cond, NULL);
}

static void lock_take(Autoplay *a)
{
    pthread_mutex_lock(&a->lock);
}

static void lock_give(Autoplay *a)
{
    pthread_mutex_unlock(&a->lock);
}

static void wake_all(Autoplay *a)
{
    pthread_cond_broadcast(&a->cond);
}

static void wait_wake(Autoplay *a)
{
    pthread_cond_wait(&a->cond, &a->lock);
}

static bool wait_wake_until(Autoplay *a, const struct timespec *until)
{
    return pthread_cond_timedwait(&a->cond, &a->lock, until) != ETIMEDOUT;
}

#endif /* _WIN32 */

bool autoplay_available(char *why, size_t n, char *short_why, size_t sn)
{
    /* (build 179) The C player: compiled in, its files the tree's or (the
     * stand-alone game) its pack's. */
    if (!use_python()) {
        for (size_t i = 0;
             i < sizeof PLAYER_NEEDED / sizeof PLAYER_NEEDED[0]; i++) {
            if (assetfs_pack_find(PLAYER_NEEDED[i], NULL) == NULL
                && access(PLAYER_NEEDED[i], R_OK) != 0) {
                snprintf(why, n, "%s is missing (run from ps2/)",
                         PLAYER_NEEDED[i]);
                snprintf(short_why, sn, "NO TOOLS");
                return false;
            }
        }
        why[0]       = '\0';
        short_why[0] = '\0';
        return true;
    }
    if (!python_found()) {
        snprintf(why, n, "%s", NO_PYTHON);
        snprintf(short_why, sn, "NO PYTHON3");
        return false;
    }
    /* (build 125) The stand-alone game carries them in its pack. */
    if (packed() && assetfs_pack_find(DRIVER, NULL) != NULL) {
        why[0]       = '\0';
        short_why[0] = '\0';
        return true;
    }
    for (size_t i = 0; i < sizeof NEEDED / sizeof NEEDED[0]; i++) {
        if (access(NEEDED[i], R_OK) != 0) {
            snprintf(why, n, "%s is missing (run from ps2/)", NEEDED[i]);
            snprintf(short_why, sn, "NO TOOLS");
            return false;
        }
    }
    why[0]       = '\0';
    short_why[0] = '\0';
    return true;
}

/* ---- the host the interpreter runs against ---- */

static void write_all(int fd, const char *p, size_t n)
{
    while (n > 0 && fd >= 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w <= 0) {
            return;             /* the driver is gone: nobody to tell */
        }
        p += w;
        n -= (size_t)w;
    }
}

static void host_log(void *ctx, u32 tick, const char *line)
{
    Autoplay *a = (Autoplay *)ctx;
    if (g_trace != NULL) {
        fprintf(g_trace, "%7u  %s\n", tick, line);
    }
    /* (build 179) the C player reads it back in process */
    if (a->session != NULL) {
        player_log_line(a->session, tick, line);
        return;
    }
    char buf[PROBE_LINE_MAX * 2 + 32];
    int  n = snprintf(buf, sizeof buf, "%7u  %s\n", tick, line);
    if (n < 0) {
        return;
    }
    if ((size_t)n >= sizeof buf) {
        n = (int)sizeof buf - 1;
        buf[n - 1] = '\n';
    }
    write_all(a->log_fd, buf, (size_t)n);
}

/* Called on the command thread, which holds the lock: post the tick and
 * wait until the frame loop has run it. A hand-back while waiting ends
 * the thread here, so the interpreter never sees the game again. */
static void end_here(Autoplay *a)
{
    /* (build 179) The C player's run jumps to its end, the lock let go:
     * it writes its report there and its thread ends (player_thread). */
    if (a->session != NULL) {
        lock_give(a);
        player_hand_back();
    }
    a->state = AUTOPLAY_FINISHED;
    wake_all(a);
    lock_give(a);
    pthread_exit(NULL);
}

static void host_tick(void *ctx, const Input *in)
{
    Autoplay *a = (Autoplay *)ctx;
    /* Handed back while the last tick was being run (DONE, the thread
     * not yet awake): nothing will serve another. */
    if (a->abort) {
        end_here(a);
    }
    a->want  = *in;
    a->state = AUTOPLAY_REQUEST;
    wake_all(a);
    while (a->state == AUTOPLAY_REQUEST && !a->abort) {
        wait_wake(a);
    }
    if (a->abort || a->state == AUTOPLAY_ABORT) {
        end_here(a);
    }
    a->state = AUTOPLAY_RUNNING;
}

/* (build 120) A host line from the driver: `@` and a word or two, for the
 * game that hosts it and not the route language -- the interpreter never
 * sees it, and it runs no tick. `@grind on` and `@grind off` bracket the
 * driver's grinds; (build 162) SPEED AUTO follows the story's beats
 * instead (main.c, game_story_beat), so they are taken and left unread.
 * (build 162a) `@gear on` and `@gear off` bracket a visit to a weapon or
 * armour store that buys, which AUTO plays at its own speed. Called
 * holding the lock. */
static void host_line(Autoplay *a, const char *line)
{
    if (strncmp(line, "@gear on", 8) == 0) {
        a->gear = true;
    } else if (strncmp(line, "@gear off", 9) == 0) {
        a->gear = false;
    } else if (strncmp(line, "@grind on", 9) != 0
               && strncmp(line, "@grind off", 10) != 0) {
        fprintf(stderr, "computer plays: unknown host line %s", line);
    }
}

static void *command_thread(void *arg)
{
    Autoplay *a = (Autoplay *)arg;
    FILE *in = fdopen(a->cmd_fd, "r");
    char  line[PROBE_LINE_MAX];
    int   lineno = 0;

    lock_take(a);
    for (;;) {
        a->state = AUTOPLAY_PIPE;
        wake_all(a);
        lock_give(a);

        char *got = (in != NULL) ? fgets(line, sizeof line, in) : NULL;

        lock_take(a);
        if (got == NULL || a->abort) {
            break;
        }
        if (line[0] == '@') {
            host_line(a, line);
            continue;
        }
        a->state = AUTOPLAY_RUNNING;
        lineno++;
        (void)probe_run_text(line, lineno, g_dir, g_slots);
    }
    a->state = AUTOPLAY_FINISHED;
    wake_all(a);
    lock_give(a);
    if (in != NULL) {
        fclose(in);
    }
    a->cmd_fd = -1;
    return NULL;
}

/* (build 179) One of the C player's lines (PlayerIO.run), on its thread:
 * as command_thread runs one of the driver's -- a host line kept from the
 * interpreter, a command run holding the game -- with the lock let go
 * between them, while the player thinks. A hand-back seen here ends the
 * run as host_tick's does. */
static void player_line(void *ctx, const char *line)
{
    Autoplay *a = (Autoplay *)ctx;
    char buf[PROBE_LINE_MAX];
    snprintf(buf, sizeof buf, "%s\n", line);
    lock_take(a);
    if (a->abort) {
        lock_give(a);
        player_hand_back();
    }
    if (buf[0] == '@') {
        host_line(a, buf);
        lock_give(a);
        return;
    }
    a->state = AUTOPLAY_RUNNING;
    a->lineno++;
    (void)probe_run_text(buf, a->lineno, g_dir, g_slots);
    a->state = AUTOPLAY_PIPE;
    wake_all(a);
    lock_give(a);
}

/* (build 179) The C player's thread: COMPUTER PLAYS's loop (player.h,
 * src/player/computer.c), on a stack of its own (PLAYER_STACK). Until its
 * first line it only thinks, so the frame loop may draw (PIPE). */
static void *player_thread(void *arg)
{
    Autoplay *a = (Autoplay *)arg;
    lock_take(a);
    a->state = AUTOPLAY_PIPE;
    wake_all(a);
    lock_give(a);
    (void)player_computer_run(a->session);
    lock_take(a);
    a->state = AUTOPLAY_FINISHED;
    wake_all(a);
    lock_give(a);
    return NULL;
}

/* (build 179) The C player's arguments, computer_plays.py's: --start,
 * --out the run's directory, then PS2_COMPUTER_ARGS's. Kept for the run,
 * which reads them as it goes. False when they do not parse (said on
 * stderr). */
static bool player_args(const char *start, const char *extra)
{
    static char  words[1024];
    static char  start_buf[HOME_PATH * 2];
    static char *argv[DRIVER_ARGS + 1];
    int argc = 0;
    snprintf(start_buf, sizeof start_buf, "%s", start);
    argv[argc++] = "--start";
    argv[argc++] = start_buf;
    argv[argc++] = "--out";
    argv[argc++] = g_dir;
    snprintf(words, sizeof words, "%s", (extra != NULL) ? extra : "");
    for (char *w = strtok(words, " \t"); w != NULL && argc < DRIVER_ARGS;
         w = strtok(NULL, " \t")) {
        argv[argc++] = w;
    }
    argv[argc] = NULL;
    player_set_echo(stderr);
    bool ok = player_computer_parse(argc, argv);
    player_set_echo(NULL);
    return ok;
}

/* ---- the run's own directory ---- */

static void make_dir(const char *path)
{
    if (make_one_dir(path) != 0 && errno != EEXIST) {
        fprintf(stderr, "computer plays: cannot make %s\n", path);
    }
}

/* Every file in `dir` (not the directory): the last run's slots,
 * checkpoints. */
static void clear_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (d == NULL) {
        return;
    }
    struct dirent *e;
    char path[1024];
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            remove(path);
        }
    }
    closedir(d);
}

bool autoplay_start(Autoplay *a, Game *g, Video *v, const char *start,
                    const char *speed, const char *extra)
{
    static bool inited;
    if (!inited) {
        lock_init(a);
        inited = true;
    }
    a->state       = AUTOPLAY_OFF;
    a->abort       = false;
    a->thread_live = false;
    a->child       = -1;
#ifdef _WIN32
    a->process     = NULL;
#endif
    a->cmd_fd      = -1;
    a->log_fd      = -1;
    a->game        = g;
    a->video       = v;
    a->ticks       = 0;
    a->gear        = false;
    a->python      = use_python();      /* (build 179) */
    a->session     = NULL;
    a->lineno      = 0;

    /* (build 179) The C player's arguments first: ones that do not parse
     * leave the last run as it was. */
    if (!a->python && !player_args(start, extra)) {
        return false;
    }

    char checkpoints[HOME_PATH + 16];
    snprintf(checkpoints, sizeof checkpoints, "%s/checkpoints", g_dir);
    make_dir(g_home);
    make_dir(g_dir);
    make_dir(g_slots);
    make_dir(checkpoints);
    /* (build 103) Going on from the computer's last run keeps its saves
     * (last.json names the one it goes on from); any other start clears
     * the last run first. */
    if (strcmp(start, "last") != 0) {
        clear_dir(g_slots);
        clear_dir(checkpoints);
        clear_dir(g_dir);
    }

    /* (build 179) The C player: its session, the story's lines to
     * driver.log as the Python's stdout was. */
    if (!a->python) {
        char log_path[HOME_PATH + 16];
        snprintf(log_path, sizeof log_path, "%s/driver.log", g_dir);
        g_echo = fopen(log_path, "wb");
        player_set_echo(g_echo);
        PlayerIO io = { player_line, a, true };
        a->session = player_session_new("computer", &io);
    }
    const char *trace = getenv("PS2_COMPUTER_LOG");
    if (trace != NULL && trace[0] != '\0') {
        g_trace = fopen(trace, "wb");
    }

    /* (build 125) The driver's files and paths: the tree's as they are,
     * or the pack's written out and every path whole. */
    snprintf(g_driver, sizeof g_driver, "%s", DRIVER);
    snprintf(g_out, sizeof g_out, "%s", g_dir);
    g_harness[0] = '\0';
    if (a->python && packed()) {
        char dir[HOME_PATH + 16];
        snprintf(dir, sizeof dir, "%s/harness", g_dir);
        whole_path(g_harness, sizeof g_harness, dir);
        if (!write_harness(g_harness)) {
            return false;
        }
        snprintf(g_driver, sizeof g_driver, "%s/%s", g_harness, DRIVER);
        whole_path(g_out, sizeof g_out, g_dir);
        if (strcmp(start, "new") != 0 && strcmp(start, "last") != 0) {
            whole_path(g_start, sizeof g_start, start);
            start = g_start;
        }
    }

    if (a->python && !spawn_driver(a, start, extra)) {
        return false;
    }

    /* The interpreter, against this host; the game's saves are the run's. */
    gamedata_set_save_dir(g_slots);
    ProbeHost host;
    memset(&host, 0, sizeof host);
    host.game        = g;
    host.video       = v;
    host.tick        = host_tick;
    host.log         = host_log;
    host.player_only = true;
    host.ctx         = a;
    probe_attach(&host);
    game_debug_on_script(g, probe_on_script, NULL);
    audio_debug_on_sound(probe_on_sound, NULL);
    probe_begin();

    a->state = AUTOPLAY_RUNNING;
    int rc;
    if (a->python) {
        rc = pthread_create(&a->thread, NULL, command_thread, a);
    } else {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, PLAYER_STACK);
        rc = pthread_create(&a->thread, &attr, player_thread, a);
        pthread_attr_destroy(&attr);
    }
    if (rc != 0) {
        a->abort = true;
        autoplay_stop(a);
        return false;
    }
    a->thread_live = true;
    if (a->python) {
        fprintf(stderr, "computer plays: from %s, %s speed (pid %d)\n",
                start, speed, (int)a->child);
    } else {
        fprintf(stderr, "computer plays: from %s, %s speed (the C player)\n",
                start, speed);
    }
    return true;
}

bool autoplay_running(const Autoplay *a)
{
    return a->thread_live || a->child > 0;
}

bool autoplay_buying_gear(const Autoplay *a)
{
    return a->gear;
}

AutoplayHold autoplay_hold(Autoplay *a, Input *in, double wait_s,
                           bool want_tick)
{
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    long ns = until.tv_nsec + (long)(wait_s * 1e9);
    until.tv_sec  += ns / 1000000000L;
    until.tv_nsec  = ns % 1000000000L;

    /* Through the driver's thinking too (PIPE): a frame is for ticks, and
     * the next one usually comes within a millisecond or two. */
    lock_take(a);
    while (a->state == AUTOPLAY_RUNNING || a->state == AUTOPLAY_DONE
           || (want_tick && a->state == AUTOPLAY_PIPE)) {
        if (!wait_wake_until(a, &until)) {
            break;
        }
    }
    switch (a->state) {
        case AUTOPLAY_REQUEST: {
            *in = a->want;
            return AUTOPLAY_HOLD_TICK;
        }
        case AUTOPLAY_PIPE: {
            return AUTOPLAY_HOLD_IDLE;
        }
        case AUTOPLAY_FINISHED: {
            lock_give(a);
            return AUTOPLAY_HOLD_ENDED;
        }
        case AUTOPLAY_OFF: {
            lock_give(a);
            return AUTOPLAY_HOLD_ENDED;
        }
        default: {
            lock_give(a);
            return AUTOPLAY_HOLD_BUSY;
        }
    }
}

void autoplay_release(Autoplay *a, bool ticked)
{
    if (ticked) {
        a->ticks++;
        a->state = AUTOPLAY_DONE;
        wake_all(a);
    }
    lock_give(a);
}

void autoplay_stop(Autoplay *a)
{
    lock_take(a);
    a->abort = true;
    if (a->state == AUTOPLAY_REQUEST) {
        a->state = AUTOPLAY_ABORT;
    }
    wake_all(a);
    /* The driver reads EOF on its log pipe and writes its report. */
    if (a->log_fd >= 0) {
        close(a->log_fd);
        a->log_fd = -1;
    }
    lock_give(a);

    if (a->child > 0) {
        reap_driver(a);
        a->child = -1;
    }
    /* With the driver gone its command pipe reads EOF, so the thread is
     * out of the interpreter -- or it ended in host_tick above. */
    if (a->thread_live) {
        pthread_join(a->thread, NULL);
        a->thread_live = false;
    }
    if (a->cmd_fd >= 0) {
        close(a->cmd_fd);        /* the thread ended inside a tick */
        a->cmd_fd = -1;
    }
    /* (build 179) The C player's run has written its report and ended. */
    player_session_free(a->session);
    a->session = NULL;
    if (g_echo != NULL) {
        player_set_echo(NULL);
        fclose(g_echo);
        g_echo = NULL;
    }
    if (g_trace != NULL) {
        fclose(g_trace);
        g_trace = NULL;
    }
    game_debug_on_script(a->game, NULL, NULL);
    audio_debug_on_sound(NULL, NULL);
    gamedata_set_save_dir(NULL);
    a->state = AUTOPLAY_OFF;
    fprintf(stderr, "computer plays: the pad is yours again after %u "
                    "ticks (%s/report.txt)\n", a->ticks, g_dir);
}
