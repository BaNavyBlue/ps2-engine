/* (build 193) Files by their UTF-8 names (extract.h).
 *
 * Every path the extraction is given is UTF-8: SDL hands a dropped or
 * chosen file's so on every system, and the command line's are read as
 * UTF-8 (ex_utf8_args). On Linux and macOS a name is its bytes, and the
 * C library's calls take them as they are. On Windows a narrow name is
 * read in the ANSI code page, which cannot hold every name a player's
 * file may have ("Phantasy Star II (Kaerazaru Toki no Owari ni)" in
 * Japanese, an accented folder), so each path is turned into the wide
 * name Windows keeps and opened by that: _wfopen, _wstat64, _wmkdir,
 * _wremove, FindFirstFileW. A path that is not UTF-8 is taken as it is,
 * by the narrow calls, as before.
 *
 * Both sides of each #ifdef are written; the POSIX side is opendir and
 * stat, which glibc, macOS and MinGW all have. */

#define _POSIX_C_SOURCE 200809L

#include "extract.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifdef _WIN32

/* The wide name of a UTF-8 one (malloc'd), or NULL when it is not UTF-8
 * (or there is no memory): then the narrow call takes it as it is. */
static wchar_t *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (n <= 0) {
        return NULL;
    }
    wchar_t *w = malloc(sizeof *w * (size_t)n);
    if (w == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) <= 0) {
        free(w);
        return NULL;
    }
    return w;
}

FILE *ex_fopen(const char *path, const char *mode)
{
    wchar_t *w = wide(path);
    if (w == NULL) {
        return fopen(path, mode);
    }
    wchar_t wmode[8];
    size_t  i = 0;
    for (; mode[i] != '\0' && i + 1 < sizeof wmode / sizeof wmode[0]; i++) {
        wmode[i] = (wchar_t)(unsigned char)mode[i];
    }
    wmode[i] = L'\0';
    FILE *fh = _wfopen(w, wmode);
    free(w);
    return fh;
}

static ExPathKind kind_of(unsigned mode)
{
    if ((mode & _S_IFMT) == _S_IFREG) {
        return EX_PATH_FILE;
    }
    if ((mode & _S_IFMT) == _S_IFDIR) {
        return EX_PATH_DIR;
    }
    return EX_PATH_OTHER;
}

ExPathKind ex_path_kind(const char *path, long long *size)
{
    struct _stat64 st;
    wchar_t *w = wide(path);
    int r = (w != NULL) ? _wstat64(w, &st) : _stat64(path, &st);
    free(w);
    if (r != 0) {
        return EX_PATH_NONE;
    }
    if (size != NULL) {
        *size = (long long)st.st_size;
    }
    return kind_of((unsigned)st.st_mode);
}

int ex_mkdir_one(const char *path)
{
    wchar_t *w = wide(path);
    int r = (w != NULL) ? _wmkdir(w) : _mkdir(path);
    free(w);
    return r;
}

int ex_remove_file(const char *path)
{
    wchar_t *w = wide(path);
    int r = (w != NULL) ? _wremove(w) : remove(path);
    free(w);
    return r;
}

/* A folder's names: FindFirstFileW's, each turned back into UTF-8 (the
 * narrow FindFirstFileA's, in the ANSI code page, for a path that is not
 * UTF-8). */
struct ExDir {
    HANDLE           find;
    bool             is_wide;
    bool             first;      /* the find's first entry not yet read */
    WIN32_FIND_DATAW wdata;
    WIN32_FIND_DATAA adata;
    char             name[MAX_PATH * 4];
};

ExDir *ex_dir_open(const char *path)
{
    size_t n = strlen(path);
    bool   sep = n > 0 && (path[n - 1] == '/' || path[n - 1] == '\\');
    char  *pattern = malloc(n + 3);
    if (pattern == NULL) {
        return NULL;
    }
    memcpy(pattern, path, n);
    if (sep || n == 0) {
        memcpy(pattern + n, "*", 2);
    } else {
        memcpy(pattern + n, "\\*", 3);
    }
    ExDir *d = calloc(1, sizeof *d);
    if (d == NULL) {
        free(pattern);
        return NULL;
    }
    wchar_t *w = wide(pattern);
    if (w != NULL) {
        d->is_wide = true;
        d->find = FindFirstFileW(w, &d->wdata);
        free(w);
    } else {
        d->find = FindFirstFileA(pattern, &d->adata);
    }
    free(pattern);
    if (d->find == INVALID_HANDLE_VALUE) {
        free(d);
        return NULL;
    }
    d->first = true;
    return d;
}

const char *ex_dir_next(ExDir *d)
{
    for (;;) {
        if (!d->first) {
            BOOL more = d->is_wide ? FindNextFileW(d->find, &d->wdata)
                                   : FindNextFileA(d->find, &d->adata);
            if (!more) {
                return NULL;
            }
        }
        d->first = false;
        if (d->is_wide) {
            if (WideCharToMultiByte(CP_UTF8, 0, d->wdata.cFileName, -1,
                                    d->name, (int)sizeof d->name, NULL,
                                    NULL) <= 0) {
                continue;
            }
        } else {
            snprintf(d->name, sizeof d->name, "%s", d->adata.cFileName);
        }
        if (strcmp(d->name, ".") != 0 && strcmp(d->name, "..") != 0) {
            return d->name;
        }
    }
}

void ex_dir_close(ExDir *d)
{
    if (d != NULL) {
        FindClose(d->find);
        free(d);
    }
}

/* The wide command line's arguments by the C runtime's rules (as
 * CommandLineToArgvW reads them: the program's name up to its closing
 * quote or the first blank; then backslashes doubled before a quote,
 * quotes toggling a quoted run, "" inside one a quote), each in UTF-8,
 * one after another in `out`; the count. `out` has room for all of them:
 * three bytes a wide character at most, and one for each end. */
static int wide_arguments(const wchar_t *p, char **ptrs, char *out,
                          size_t room)
{
    int     count = 0;
    size_t  used  = 0;
    size_t  len   = wcslen(p);
    wchar_t *arg  = malloc(sizeof *arg * (len + 1));
    if (arg == NULL) {
        return 0;
    }
    bool program = true;
    for (;;) {
        while (*p == L' ' || *p == L'\t') {
            p++;
        }
        if (*p == L'\0') {
            break;
        }
        size_t n = 0;
        if (program) {
            if (*p == L'"') {
                p++;
                while (*p != L'\0' && *p != L'"') {
                    arg[n++] = *p++;
                }
                if (*p == L'"') {
                    p++;
                }
            } else {
                while (*p != L'\0' && *p != L' ' && *p != L'\t') {
                    arg[n++] = *p++;
                }
            }
            program = false;
        } else {
            bool quoted = false;
            while (*p != L'\0' && (quoted || (*p != L' ' && *p != L'\t'))) {
                size_t slashes = 0;
                while (*p == L'\\') {
                    slashes++;
                    p++;
                }
                if (*p == L'"') {
                    for (size_t s = 0; s < slashes / 2; s++) {
                        arg[n++] = L'\\';
                    }
                    if (slashes % 2 == 1) {
                        arg[n++] = L'"';
                    } else if (quoted && p[1] == L'"') {
                        arg[n++] = L'"';
                        p++;
                    } else {
                        quoted = !quoted;
                    }
                    p++;
                } else {
                    for (size_t s = 0; s < slashes; s++) {
                        arg[n++] = L'\\';
                    }
                    if (*p != L'\0'
                        && (quoted || (*p != L' ' && *p != L'\t'))) {
                        arg[n++] = *p++;
                    }
                }
            }
        }
        arg[n] = L'\0';
        int got = WideCharToMultiByte(CP_UTF8, 0, arg, -1, out + used,
                                      (int)(room - used), NULL, NULL);
        if (got <= 0) {
            break;
        }
        ptrs[count++] = out + used;
        used += (size_t)got;
    }
    free(arg);
    return count;
}

char **ex_utf8_args(int n, char **args)
{
    if (n < 0) {
        n = 0;
    }
    const wchar_t *line = GetCommandLineW();
    size_t len = (line != NULL) ? wcslen(line) : 0;
    /* an argument takes a character of the line at least, and its blank */
    size_t most = len / 2 + 2;
    if (most < (size_t)n + 1) {
        most = (size_t)n + 1;
    }
    size_t room = 3 * len + most + 1;
    char **ptrs = malloc(sizeof *ptrs * (most + 1) + room);
    if (ptrs == NULL) {
        return NULL;
    }
    char *text = (char *)(ptrs + most + 1);
    int count = (line != NULL) ? wide_arguments(line, ptrs, text, room) : 0;
    if (count >= n) {
        /* the last n: what main() was handed past the ones it kept */
        memmove(ptrs, ptrs + (count - n), sizeof *ptrs * (size_t)n);
    } else {
        for (int i = 0; i < n; i++) {
            ptrs[i] = args[i];
        }
    }
    ptrs[n] = NULL;
    return ptrs;
}

#else /* POSIX */

FILE *ex_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

ExPathKind ex_path_kind(const char *path, long long *size)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return EX_PATH_NONE;
    }
    if (size != NULL) {
        *size = (long long)st.st_size;
    }
    if (S_ISREG(st.st_mode)) {
        return EX_PATH_FILE;
    }
    if (S_ISDIR(st.st_mode)) {
        return EX_PATH_DIR;
    }
    return EX_PATH_OTHER;
}

int ex_mkdir_one(const char *path)
{
    return mkdir(path, 0777);
}

int ex_remove_file(const char *path)
{
    return remove(path);
}

struct ExDir {
    DIR *dir;
};

ExDir *ex_dir_open(const char *path)
{
    DIR *dir = opendir((path[0] != '\0') ? path : ".");
    if (dir == NULL) {
        return NULL;
    }
    ExDir *d = malloc(sizeof *d);
    if (d == NULL) {
        closedir(dir);
        return NULL;
    }
    d->dir = dir;
    return d;
}

const char *ex_dir_next(ExDir *d)
{
    struct dirent *e;
    while ((e = readdir(d->dir)) != NULL) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            return e->d_name;
        }
    }
    return NULL;
}

void ex_dir_close(ExDir *d)
{
    if (d != NULL) {
        closedir(d->dir);
        free(d);
    }
}

/* The arguments are their bytes already: the same pointers. */
char **ex_utf8_args(int n, char **args)
{
    if (n < 0) {
        n = 0;
    }
    char **ptrs = malloc(sizeof *ptrs * ((size_t)n + 1));
    if (ptrs == NULL) {
        return NULL;
    }
    for (int i = 0; i < n; i++) {
        ptrs[i] = args[i];
    }
    ptrs[n] = NULL;
    return ptrs;
}

#endif
