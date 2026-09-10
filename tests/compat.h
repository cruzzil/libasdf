/**
 * The POSIX surface the test utilities use, for targets that do not have it.
 *
 * On POSIX this is the real headers plus two thin helpers.  On Windows it
 * supplies directory iteration and process-group identity in terms of the
 * Win32 equivalents.
 *
 * The "process group" here is only ever used as a key: every test binary in
 * one `ctest` run wants the same run directory, and wants to recognise
 * coordination files left behind by runs that have since exited.  The parent
 * process id serves that purpose on Windows exactly as the process group id
 * does on POSIX -- ctest is the common parent of every test binary it starts.
 */
#ifndef ASDF_TESTS_COMPAT_H
#define ASDF_TESTS_COMPAT_H

#if !defined(_WIN32)

#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

/** The key shared by every test binary in one run. */
static inline int asdf_test_group_id(void) {
    return (int)getpgrp();
}

/** Is the run that wrote this key still going? */
static inline int asdf_test_group_alive(int group) {
    return !(kill(-(pid_t)group, 0) == -1 && errno == ESRCH);
}

#define asdf_test_mkdir(path) mkdir((path), 0777)
#define asdf_test_symlink(target, link) symlink((target), (link))

#else /* _WIN32 */

#include <direct.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <tlhelp32.h>
#include <windows.h>

#if !defined(PATH_MAX)
#define PATH_MAX _MAX_PATH
#endif

/* <dirent.h>, as much of it as the test utilities read. */
struct dirent {
    char d_name[MAX_PATH];
};

typedef struct {
    HANDLE handle;
    WIN32_FIND_DATAA data;
    int first;
    struct dirent entry;
} DIR;

static inline DIR *opendir(const char *path) {
    char pattern[MAX_PATH];

    if (snprintf(pattern, sizeof(pattern), "%s\\*", path) < 0)
        return NULL;

    DIR *dir = (DIR *)calloc(1, sizeof(DIR));

    if (!dir)
        return NULL;

    dir->handle = FindFirstFileA(pattern, &dir->data);

    if (dir->handle == INVALID_HANDLE_VALUE) {
        free(dir);
        return NULL;
    }

    dir->first = 1;
    return dir;
}

static inline struct dirent *readdir(DIR *dir) {
    if (!dir)
        return NULL;

    if (dir->first)
        dir->first = 0;
    else if (!FindNextFileA(dir->handle, &dir->data))
        return NULL;

    strncpy(dir->entry.d_name, dir->data.cFileName, sizeof(dir->entry.d_name) - 1);
    dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = '\0';
    return &dir->entry;
}

static inline int closedir(DIR *dir) {
    if (!dir)
        return -1;

    FindClose(dir->handle);
    free(dir);
    return 0;
}

/*
 * The parent process id: ctest starts every test binary in a run, so its id
 * groups them the way a process group id does on POSIX.
 */
static inline int asdf_test_group_id(void) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE)
        return (int)GetCurrentProcessId();

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);
    DWORD self = GetCurrentProcessId();
    int parent = (int)self;

    if (Process32First(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == self) {
                parent = (int)entry.th32ParentProcessID;
                break;
            }
        } while (Process32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return parent;
}

static inline int asdf_test_group_alive(int group) {
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)group);

    if (!proc)
        return 0;

    /*
     * An id is reused once its process is gone, so a live handle alone is not
     * enough -- check it has not already exited.
     */
    DWORD code = 0;
    int alive = GetExitCodeProcess(proc, &code) && code == STILL_ACTIVE;
    CloseHandle(proc);
    return alive;
}

#define asdf_test_mkdir(path) _mkdir(path)
/*
 * Symlinks need a privilege the runner does not grant by default, and the
 * caller treats this as best-effort, so report failure rather than pretend.
 */
#define asdf_test_symlink(target, link) (-1)

#define unlink(path) _unlink(path)
#define open _open
#define close _close
#define write _write
#define read _read

#endif /* _WIN32 */

#endif /* ASDF_TESTS_COMPAT_H */
