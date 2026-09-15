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

static inline int asdf_mkstemp(char *template_) {
    return mkstemp(template_);
}

#else /* _WIN32 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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
 * `close` is a named helper, never a macro: `asdf_stream` has members called
 * `close`, `read` and `write`, which any macro of those names would rewrite,
 * and `#define read _read` also breaks `#pragma section(".CRT$XCU", read)`.
 */
static inline int asdf_close_fd(int fd) {
    return _close(fd);
}

/*
 * Time conversion. The CRT's _mkgmtime and gmtime both refuse any date before
 * 1970 -- and libasdf parses dates well before that; a Julian-epoch year of
 * 1948 is in the test suite -- and there is no strptime at all, so every string
 * time came back as year 1900 with a zero timestamp. These are plain C with no
 * CRT time calls, and were checked against glibc's gmtime_r, timegm and
 * strptime: gmtime over negative times out to year 1 and 9999, timegm with
 * out-of-range fields normalised the way glibc normalises them, and strptime
 * over the formats time.c uses, including where it leaves the rest pointer.
 */
/* Days since 1970-01-01 for a proleptic Gregorian date (Hinnant). */
static inline long long asdf_compat_days_from_civil_(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

static inline long long asdf_compat_floor_div_(long long a, long long b) {
    long long q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

static inline void asdf_compat_civil_from_days_(long long z, struct tm *out) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long y = (long long)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp + (mp < 10 ? 3 : -9);
    const long long year = y + (m <= 2);
    out->tm_year = (int)(year - 1900);
    out->tm_mon = (int)m - 1;
    out->tm_mday = (int)d;
    out->tm_yday = (int)(z - 719468 - asdf_compat_days_from_civil_(year, 1, 1));
}

/* gmtime into a caller buffer, valid for negative times. */
static inline struct tm *asdf_compat_gmtime_r(const time_t *t, struct tm *out) {
    if (!t || !out)
        return NULL;
    long long secs = (long long)*t;
    long long days = asdf_compat_floor_div_(secs, 86400);
    long long rem = secs - days * 86400;
    memset(out, 0, sizeof(*out));
    asdf_compat_civil_from_days_(days, out);
    out->tm_hour = (int)(rem / 3600);
    out->tm_min = (int)(rem % 3600 / 60);
    out->tm_sec = (int)(rem % 60);
    out->tm_wday = (int)((days % 7 + 11) % 7); /* 1970-01-01 was a Thursday */
    out->tm_isdst = 0;
    return out;
}

/* timegm: normalises the struct in place, as glibc's does. */
static inline time_t asdf_compat_timegm(struct tm *tm) {
    if (!tm)
        return (time_t)-1;
    long long year = (long long)tm->tm_year + 1900;
    long long mon = tm->tm_mon;
    year += asdf_compat_floor_div_(mon, 12);
    mon -= asdf_compat_floor_div_(mon, 12) * 12;
    long long days = asdf_compat_days_from_civil_(year, (unsigned)mon + 1, 1) + (tm->tm_mday - 1LL);
    long long secs = days * 86400 + tm->tm_hour * 3600LL + tm->tm_min * 60LL + tm->tm_sec;
    time_t t = (time_t)secs;
    asdf_compat_gmtime_r(&t, tm);
    return t;
}

static inline const char *asdf_compat_num_(const char *s, int max_digits, long long *out) {
    int n = 0;
    long long v = 0;
    while (n < max_digits && isdigit((unsigned char)s[n])) {
        v = v * 10 + (s[n] - '0');
        n++;
    }
    if (n == 0)
        return NULL;
    *out = v;
    return s + n;
}

/* strptime for the conversions libasdf's time parser uses: %Y %m %d %H %M %S
 * %j %s %%, literal characters, and whitespace. Times are UTC. Returns a
 * pointer just past the consumed input, or NULL on mismatch. */
static inline char *asdf_compat_strptime(const char *s, const char *fmt, struct tm *tm) {
    int have_year = 0, have_mon = 0, have_mday = 0, have_yday = 0;
    long long v = 0;

    while (*fmt) {
        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*s))
                s++;
            fmt++;
            continue;
        }
        if (*fmt != '%') {
            if (*s != *fmt)
                return NULL;
            s++, fmt++;
            continue;
        }
        fmt++;
        if (*fmt != '%' && *fmt != 's')
            while (isspace((unsigned char)*s))
                s++;
        switch (*fmt++) {
        case 'Y':
            if (!(s = asdf_compat_num_(s, 4, &v))) return NULL;
            tm->tm_year = (int)(v - 1900);
            have_year = 1;
            break;
        case 'm':
            if (!(s = asdf_compat_num_(s, 2, &v)) || v < 1 || v > 12) return NULL;
            tm->tm_mon = (int)v - 1;
            have_mon = 1;
            break;
        case 'd':
            if (!(s = asdf_compat_num_(s, 2, &v)) || v < 1 || v > 31) return NULL;
            tm->tm_mday = (int)v;
            have_mday = 1;
            break;
        case 'H':
            if (!(s = asdf_compat_num_(s, 2, &v)) || v > 23) return NULL;
            tm->tm_hour = (int)v;
            break;
        case 'M':
            if (!(s = asdf_compat_num_(s, 2, &v)) || v > 59) return NULL;
            tm->tm_min = (int)v;
            break;
        case 'S':
            if (!(s = asdf_compat_num_(s, 2, &v)) || v > 61) return NULL;
            tm->tm_sec = (int)v;
            break;
        case 'j':
            if (!(s = asdf_compat_num_(s, 3, &v)) || v < 1 || v > 366) return NULL;
            tm->tm_yday = (int)v - 1;
            have_yday = 1;
            break;
        case 's': {
            /* Digits only, as glibc reads it: a leading '-' is a mismatch. */
            if (!(s = asdf_compat_num_(s, 19, &v))) return NULL;
            time_t t = (time_t)v;
            asdf_compat_gmtime_r(&t, tm);
            have_year = have_mon = have_mday = 0;
            break;
        }
        case '%':
            if (*s != '%') return NULL;
            s++;
            break;
        default:
            return NULL;
        }
    }

    /* As glibc does: a day of the year with no month/day fixes both. */
    if (have_yday && have_year && !have_mon && !have_mday) {
        struct tm tmp;
        long long days = asdf_compat_days_from_civil_((long long)tm->tm_year + 1900, 1, 1) + tm->tm_yday;
        time_t t = (time_t)(days * 86400);
        asdf_compat_gmtime_r(&t, &tmp);
        tm->tm_mon = tmp.tm_mon;
        tm->tm_mday = tmp.tm_mday;
    }
    return (char *)s;
}


static inline struct tm *asdf_compat_gmtime_(const time_t *t) {
    static __declspec(thread) struct tm buf;
    return asdf_compat_gmtime_r(t, &buf);
}

/* <time.h> is included above, so these cannot rewrite the CRT's declarations. */
#define timegm(tm) asdf_compat_timegm(tm)
#define gmtime(t) asdf_compat_gmtime_(t)
#define strptime(s, fmt, tm) asdf_compat_strptime((s), (fmt), (tm))

/* <strings.h> */
#define strcasecmp(a, b) _stricmp((a), (b))

/* Large-file stdio, which the CRT spells with an i64 suffix. */
#define fseeko(fp, off, whence) _fseeki64((fp), (off), (whence))
#define ftello(fp) _ftelli64(fp)

/* Guarded: src/compat/posix.h and tests/compat.h both supply it, and a
 * translation unit can see both. */
#if !defined(ASDF_STRNDUP_SHIM)
#define ASDF_STRNDUP_SHIM
static inline char *strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *p = (char *)malloc(len + 1);

    if (!p)
        return NULL;

    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}
#endif

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

/* Win32 reports through GetLastError; libasdf reports through errno. */
static inline void asdf_win32_set_errno_(void) {
    switch (GetLastError()) {
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
    case ERROR_COMMITMENT_LIMIT:
        errno = ENOMEM;
        break;
    case ERROR_ACCESS_DENIED:
        errno = EACCES;
        break;
    case ERROR_INVALID_HANDLE:
        errno = EBADF;
        break;
    default:
        errno = EINVAL;
        break;
    }
}

/*
 * mmap on CreateFileMapping/MapViewOfFile.
 *
 * POSIX wants the offset aligned to the page size, and that is what callers
 * pass -- stream.c aligns to sysconf(_SC_PAGESIZE), 4 KB. MapViewOfFile wants
 * it aligned to the *allocation granularity*, 64 KB, and refuses anything
 * else. So the view starts at the enclosing 64 KB boundary and the pointer
 * handed back is advanced to the offset asked for; munmap finds the view's
 * real base again through VirtualQuery. Before this, any block whose data sat
 * past the first 4 KB but short of 64 KB failed to map -- the float64 arrays
 * in numeric.asdf and the lz4 block in compressed.asdf.
 */
static inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, long long off) {
    (void)addr;

    if (flags & MAP_ANONYMOUS) {
        void *p = VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, asdf_win32_page_prot_(prot));

        if (!p) {
            asdf_win32_set_errno_();
            return MAP_FAILED;
        }

        return p;
    }

    if (off < 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    HANDLE file = (HANDLE)_get_osfhandle(fd);

    if (file == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return MAP_FAILED;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    unsigned long long granularity = si.dwAllocationGranularity;
    unsigned long long base_off = (unsigned long long)off / granularity * granularity;
    size_t delta = (size_t)((unsigned long long)off - base_off);

    /* A private writable mapping must not write through to the file. */
    DWORD page = asdf_win32_page_prot_(prot);

    if ((flags & MAP_PRIVATE) && (prot & PROT_WRITE))
        page = PAGE_WRITECOPY;

    /* Zero size means "to the end of the file", as CreateFileMapping reads it. */
    HANDLE mapping = CreateFileMappingA(file, NULL, page, 0, 0, NULL);

    if (!mapping) {
        asdf_win32_set_errno_();
        return MAP_FAILED;
    }

    DWORD view = FILE_MAP_READ;

    if (page == PAGE_WRITECOPY)
        view = FILE_MAP_COPY;
    else if (prot & PROT_WRITE)
        view = FILE_MAP_WRITE;

    void *base = MapViewOfFile(
        mapping, view, (DWORD)(base_off >> 32), (DWORD)(base_off & 0xffffffffu), len + delta);

    if (!base)
        asdf_win32_set_errno_();

    /* The view holds its own reference, so the handle is done with here. */
    CloseHandle(mapping);
    return base ? (void *)((char *)base + delta) : MAP_FAILED;
}

static inline int munmap(void *addr, size_t len) {
    MEMORY_BASIC_INFORMATION mbi;

    (void)len;

    /*
     * The caller says neither what kind of region this is nor where it
     * starts: a view may have been handed out part-way in (see mmap), and an
     * anonymous allocation is released differently from a view. VirtualQuery
     * answers both.
     */
    if (!addr || VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) {
        errno = EINVAL;
        return -1;
    }

    BOOL ok = mbi.Type == MEM_MAPPED ? UnmapViewOfFile(mbi.AllocationBase)
                                     : VirtualFree(mbi.AllocationBase, 0, MEM_RELEASE);

    if (!ok) {
        asdf_win32_set_errno_();
        return -1;
    }

    return 0;
}

static inline int mprotect(void *addr, size_t len, int prot) {
    DWORD old = 0;
    return VirtualProtect(addr, len, asdf_win32_page_prot_(prot), &old) ? 0 : -1;
}

#endif /* _WIN32 */

#endif /* ASDF_COMPAT_POSIX_H */
