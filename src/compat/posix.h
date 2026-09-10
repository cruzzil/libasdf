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

#include <sys/mman.h>
#include <unistd.h>

#else /* _WIN32 */

#include <io.h>
#include <stdint.h>
#include <stdlib.h>

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
 * Wrappers rather than object-like macros, deliberately. `#define read _read`
 * rewrites every `read` token in every translation unit that sees it, and one
 * of them is the `read` attribute in `#pragma section(".CRT$XCU", read)` --
 * which silently became `_read`, so the section was never declared and every
 * constructor failed with C2341.
 */
static inline int asdf_posix_read_(int fd, void *buf, unsigned int n) {
    return _read(fd, buf, n);
}

static inline int asdf_posix_write_(int fd, const void *buf, unsigned int n) {
    return _write(fd, buf, n);
}

#define read(fd, buf, n) asdf_posix_read_((fd), (buf), (unsigned int)(n))
#define write(fd, buf, n) asdf_posix_write_((fd), (buf), (unsigned int)(n))
#define close(fd) _close(fd)
#define lseek(fd, off, whence) _lseeki64((fd), (off), (whence))
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
