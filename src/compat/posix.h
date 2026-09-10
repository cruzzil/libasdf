/**
 * The POSIX surface libasdf uses, for targets that do not have it.
 *
 * On POSIX this is just the real headers.  On Windows it supplies the pieces
 * libasdf actually calls -- descriptor I/O, and read-only file mapping -- in
 * terms of the Win32 equivalents.  Include this instead of <unistd.h> or
 * <sys/mman.h>.
 *
 * The asynchronous decompression path in compression/compression.c is not
 * covered: it wants eventfd, poll and raw syscalls as well, and needs a
 * design rather than a shim.
 */
#ifndef ASDF_COMPAT_POSIX_H
#define ASDF_COMPAT_POSIX_H

#if !defined(_WIN32)

#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static inline int asdf_close_fd(int fd) {
    return close(fd);
}

static inline int asdf_read_fd(int fd, void *buf, size_t n) {
    return (int)read(fd, buf, n);
}

static inline int asdf_write_fd(int fd, const void *buf, size_t n) {
    return (int)write(fd, buf, n);
}

static inline int asdf_mkstemp(char *template_) {
    return mkstemp(template_);
}

#else /* _WIN32 */

#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* MSVC has no ssize_t under any include; only off_t, via <sys/types.h>. */
#if !defined(_SSIZE_T_DEFINED)
#define _SSIZE_T_DEFINED
typedef int64_t ssize_t;
#endif

#if !defined(SSIZE_MAX)
#define SSIZE_MAX INT64_MAX
#endif

/*
 * <unistd.h>: the CRT has these under underscored names in <io.h>.
 *
 * `read`, `write` and `close` are deliberately NOT shimmed. `asdf_stream` has
 * members of those names, so any macro -- object- or function-like -- fires on
 * `stream->close(stream)` and breaks it. `#define read _read` also rewrote the
 * `read` attribute in `#pragma section(".CRT$XCU", read)`, which is what made
 * every constructor fail with C2341. Their call sites need a neutral spelling
 * instead, which is a source change rather than a shim.
 */
#define lseek(fd, off, whence) _lseeki64((fd), (off), (whence))

/*
 * Named descriptor helpers.
 *
 * `close`, `read` and `write` cannot be macros here -- `asdf_stream` has
 * members of those names and any macro fires on `stream->close(stream)` --
 * so the handful of genuine descriptor call sites use these instead.
 */
static inline int asdf_close_fd(int fd) {
    return _close(fd);
}

static inline int asdf_read_fd(int fd, void *buf, size_t n) {
    return _read(fd, buf, (unsigned int)n);
}

static inline int asdf_write_fd(int fd, const void *buf, size_t n) {
    return _write(fd, buf, (unsigned int)n);
}

/* <strings.h> */
#define strcasecmp(a, b) _stricmp((a), (b))
#define strncasecmp(a, b, n) _strnicmp((a), (b), (n))

/* Large-file stdio, which the CRT spells with an i64 suffix. */
#define fseeko(fp, off, whence) _fseeki64((fp), (off), (whence))
#define ftello(fp) _ftelli64(fp)

static inline char *strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *p = (char *)malloc(len + 1);

    if (!p)
        return NULL;

    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

static inline int vasprintf(char **out, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = _vscprintf(fmt, ap2);
    va_end(ap2);

    if (n < 0)
        return -1;

    char *p = (char *)malloc((size_t)n + 1);

    if (!p)
        return -1;

    int written = vsnprintf(p, (size_t)n + 1, fmt, ap);

    if (written < 0) {
        free(p);
        return -1;
    }

    *out = p;
    return written;
}

static inline int asprintf(char **out, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vasprintf(out, fmt, ap);
    va_end(ap);
    return n;
}

/*
 * open_memstream, on a temp file.
 *
 * Win32 has no memory-backed FILE.  The contents are handed back when the
 * stream is closed, which is what the one caller does -- write, then close,
 * then use the buffer -- so `fclose` is wrapped to notice a registered
 * stream and read it back first.  Unregistered streams pass straight
 * through.  The table is per-translation-unit, which is fine because a
 * memstream is opened and closed in the same one.
 */
#define ASDF_MEMSTREAM_MAX 8

typedef struct {
    FILE *fp;
    char **bufp;
    size_t *sizep;
} asdf_memstream_slot_;

static asdf_memstream_slot_ asdf_memstreams_[ASDF_MEMSTREAM_MAX];

static inline FILE *open_memstream(char **bufp, size_t *sizep) {
    if (!bufp || !sizep)
        return NULL;

    for (int i = 0; i < ASDF_MEMSTREAM_MAX; i++) {
        if (asdf_memstreams_[i].fp)
            continue;

        FILE *fp = NULL;

        if (tmpfile_s(&fp) != 0 || !fp)
            return NULL;

        asdf_memstreams_[i].fp = fp;
        asdf_memstreams_[i].bufp = bufp;
        asdf_memstreams_[i].sizep = sizep;
        *bufp = NULL;
        *sizep = 0;
        return fp;
    }

    return NULL;
}

static inline int asdf_memstream_fclose_(FILE *fp) {
    for (int i = 0; i < ASDF_MEMSTREAM_MAX; i++) {
        if (asdf_memstreams_[i].fp != fp)
            continue;

        long long len = (fflush(fp) == 0 && _fseeki64(fp, 0, SEEK_END) == 0) ? _ftelli64(fp) : -1;

        if (len >= 0 && _fseeki64(fp, 0, SEEK_SET) == 0) {
            char *buf = (char *)malloc((size_t)len + 1);

            if (buf) {
                size_t got = fread(buf, 1, (size_t)len, fp);
                buf[got] = '\0';
                *asdf_memstreams_[i].bufp = buf;
                *asdf_memstreams_[i].sizep = got;
            }
        }

        asdf_memstreams_[i].fp = NULL;
        return fclose(fp);
    }

    return fclose(fp);
}

#define fclose(fp) asdf_memstream_fclose_(fp)

/* The CRT spells it _MAX_PATH, in <stdlib.h>. */
#if !defined(PATH_MAX)
#define PATH_MAX _MAX_PATH
#endif

/* MSVC has no mkstemp; _mktemp_s rewrites the template in place. */
static inline int asdf_mkstemp(char *template_) {
    if (_mktemp_s(template_, strlen(template_) + 1) != 0)
        return -1;

    int fd = -1;

    /*
     * _O_TEMPORARY makes the file vanish when the last descriptor closes,
     * which is what the POSIX code achieves by unlink()ing immediately.
     */
    if (_sopen_s(
            &fd,
            template_,
            _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY | _O_TEMPORARY,
            _SH_DENYNO,
            _S_IREAD | _S_IWRITE) != 0)
        return -1;

    return fd;
}

#define access(path, mode) _access((path), (mode))
#define unlink(path) _unlink(path)
#define ftruncate(fd, len) _chsize_s((fd), (len))
#define isatty(fd) _isatty(fd)

#define _SC_PAGESIZE 1
#define _SC_PAGE_SIZE _SC_PAGESIZE

static inline long sysconf(int name) {
    SYSTEM_INFO si;

    if (name != _SC_PAGESIZE)
        return -1;

    GetSystemInfo(&si);
    return (long)si.dwPageSize;
}

/* <sys/mman.h>, as much of it as libasdf calls. */
#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FAILED ((void *)-1)

static inline DWORD asdf_win32_page_prot_(int prot) {
    if (prot & PROT_WRITE)
        return (prot & PROT_EXEC) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;

    if (prot & PROT_READ)
        return (prot & PROT_EXEC) ? PAGE_EXECUTE_READ : PAGE_READONLY;

    return PAGE_NOACCESS;
}

static inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, long long off) {
    (void)addr;

    if (flags & MAP_ANONYMOUS) {
        void *p = VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, asdf_win32_page_prot_(prot));
        return p ? p : MAP_FAILED;
    }

    HANDLE file = (HANDLE)_get_osfhandle(fd);

    if (file == INVALID_HANDLE_VALUE)
        return MAP_FAILED;

    /*
     * A private mapping must not write through to the file, which is what
     * Win32 calls copy-on-write.
     */
    DWORD page = asdf_win32_page_prot_(prot);

    if ((flags & MAP_PRIVATE) && (prot & PROT_WRITE))
        page = PAGE_WRITECOPY;

    /* Zero size means "to the end of the file", as CreateFileMapping reads it. */
    HANDLE mapping = CreateFileMappingA(file, NULL, page, 0, 0, NULL);

    if (!mapping)
        return MAP_FAILED;

    DWORD view = 0;

    if (page == PAGE_WRITECOPY)
        view = FILE_MAP_COPY;
    else if (prot & PROT_WRITE)
        view = FILE_MAP_WRITE;
    else
        view = FILE_MAP_READ;

    void *p = MapViewOfFile(
        mapping, view, (DWORD)((unsigned long long)off >> 32), (DWORD)(off & 0xffffffffu), len);

    /* The view holds its own reference, so the handle is done with here. */
    CloseHandle(mapping);
    return p ? p : MAP_FAILED;
}

static inline int munmap(void *addr, size_t len) {
    MEMORY_BASIC_INFORMATION mbi;

    (void)len;

    /*
     * A view and an anonymous allocation are released differently, and the
     * caller does not say which this is -- so ask.
     */
    if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0)
        return -1;

    if (mbi.Type == MEM_MAPPED)
        return UnmapViewOfFile(addr) ? 0 : -1;

    return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1;
}

static inline int mprotect(void *addr, size_t len, int prot) {
    DWORD old = 0;
    return VirtualProtect(addr, len, asdf_win32_page_prot_(prot), &old) ? 0 : -1;
}

#define MADV_NORMAL 0
#define MADV_WILLNEED 3
#define MADV_DONTNEED 4

static inline int madvise(void *addr, size_t len, int advice) {
    /* Advisory only: Win32 has no obligation to act, and neither do we. */
    (void)addr;
    (void)len;
    (void)advice;
    return 0;
}

#endif /* _WIN32 */

#endif /* ASDF_COMPAT_POSIX_H */
