# libasdf on Windows (MSVC)

**The library builds, and the full test suite passes.** 27 tests run on
`windows-latest` with MSVC 19.51; 26 pass and one skips by design (below).
A smoke test compiled from a developer command prompt writes an ASDF file and
reads it back. Linux (gcc and clang, with and without ASan), macOS (with and
without ASan) and the documentation build stay green, and the autotools
`make check` stays at 26/26.

The `windows` job in `.github/workflows/cmake.yml` reproduces it on
`workflow_dispatch`. **Private branch, not for upstream as-is.**

## What stays off, and why

| | |
|---|---|
| The `asdf` CLI | `main.c` needs `argp`, which has no MSVC port. Configure probes for `<argp.h>` and turns `ENABLE_TOOL` off with a message. The shell tests drive the CLI, so they need `ENABLE_TOOL` too. |
| `test-symbol-leakage` | Skips (exit 77) by its own checks: it reads an ELF/Mach-O symbol table with `nm`. The Windows equivalent would use `dumpbin /exports`. |
| lz4 `read_compressed_reference_file` | Skips on **every** platform: that reference file has no lz4 block. Not a Windows gap. |
| `compressed_block_no_hang_on_segfault` | Built on `sigaction`/`siglongjmp`, and exercises lazy decompression, which rests on `userfaultfd`. Neither exists on Windows. |

## Known limitations

- **File offsets past 2 GB.** `off_t` is a 32-bit `long` on MSVC, and it types
  `block.h`'s `header_pos`/`data_pos` and the stream's `seek`, `tell` and
  `open_mem`. Fixing it means changing internal types; not done here.
- **Lazy decompression** is Linux-only (`userfaultfd`); Windows uses the eager
  and decompress-to-file paths, which both pass.

## Environment

- **libfyaml** is vendored and built with MSVC (it supports it — see its
  `doc/windows-support.txt`).
- **zlib, bzip2, lz4 and pkgconf** come from vcpkg. pkg-config comes from
  vcpkg too: Chocolatey's `pkgconfiglite` began installing "0/0 packages", and
  without a pkg-config every `pkg_check_modules` silently skips.
- **PowerShell and cmd throughout.** Git Bash is on the runner, but it rewrites
  POSIX paths into Windows ones on the way to native programs, so a bash job
  partly tests MSYS rather than the build.

## Required of anyone using these headers with MSVC

- **`/Zc:preprocessor`.** `asdf_open` and `asdf_write_to` are `_Generic` macros
  dispatched on a `__VA_ARGS__` argument count; the traditional preprocessor
  miscounts and silently picks the `FILE *` overload for a filename.
- **`/std:c11 /experimental:c11atomics`** for libasdf's own sources
  (`<stdatomic.h>`). Scoped per target — see munit below.

## Bugs found that are not Windows-specific

Candidates for upstream; none sent from this branch except where noted.

- **`HAVE_STRPTIME` guarded far too much.** It wrapped the Julian Date constants
  and thirteen arithmetic time parsers, while the format dispatch outside called
  them unconditionally. On any platform without `strptime` the file failed to
  compile, then failed to link. Reproducible on Linux by clearing
  `HAVE_STRPTIME` in `config.h`.
- **A failed block map crashed the process.** `asdf_block_data_impl` passed a
  NULL from `stream->open_mem` straight to the decompressor, which dereferenced
  it. It now returns NULL.
- **Extension functions marked `ASDF_EXPORT`.** `ASDF_REGISTER_EXTENSION`
  generates 22 function *definitions* in the extension's own translation unit,
  but `ASDF_EXPORT` is `dllimport` to anyone including the header, and a
  dllimport function cannot be defined. Now `ASDF_EXT_EXPORT`.
- **`UNUSED(x)`**, in both `src/util.h` and `tests/munit.h`, expanded to
  `(void)(x)` off GCC — a syntax error in the parameter lists it is used in.
- **`-DUSE_STATGRAB=OFF` did nothing.** Sent upstream: asdf-format/libasdf#262,
  PR #263.
- **`core/time.h` included `<sys/time.h>`** for a type `<time.h>` provides.
  Filed: asdf-format/libasdf#261.

## What the Windows port needed

- **`src/compat/posix.h`**, standing in for `<unistd.h>` and `<sys/mman.h>`:
  - `mmap` aligns its view to the **allocation granularity (64 KB)** that
    `MapViewOfFile` demands, not the 4 KB page size callers align to, and hands
    back a pointer advanced to the requested offset; `munmap` releases from
    `AllocationBase`. Before this, any block past the first 4 KB but short of
    64 KB failed to map.
  - `timegm`, `gmtime` and `strptime` in plain C: the CRT's `_mkgmtime` and
    `gmtime` refuse dates before 1970, and there is no `strptime`. Checked
    against glibc under ASan/UBSan across negative times, years 1 and 9999,
    field normalisation, and every format `time.c` parses.
  - `open_memstream`, `strndup`, `asprintf`, `fseeko`/`ftello`, `ssize_t`,
    `PATH_MAX`, and errno set from `GetLastError`.
- **Exports**: `ASDF_EXPORT` is `dllexport`/`dllimport` on MSVC, selected by
  `ASDF_BUILDING_DLL`.
- **`ASDF_CONSTRUCTOR`/`ASDF_DESTRUCTOR`** take the function name, so one
  spelling covers `__attribute__((constructor))` and `.CRT$XCU`.
- **Logs** strip the source root from `__FILE__` at runtime on MSVC, which
  ignores `-fmacro-prefix-map`.
- **The temp directory** falls back to `TEMP`/`TMP`, not just `/tmp`.
- **`.gitattributes`** keeps `tests/fixtures` byte-exact: Git for Windows'
  default autocrlf rewrote 44 fixtures to CRLF.
- **Tests**: `tests/compat.h` (dirent, process groups via the parent PID,
  `fmemopen`, `memmem`, and descriptor helpers); a Python generator for the C++
  header test in place of a shell script, shared by both build systems; munit
  given `__PGI`; and `asdf.dll` put on `PATH` for the doc tests.

## Traps worth remembering

- **Never macro-define `read`, `write`, `close` or `open` — in either form.**
  Object-like, `#define read _read` rewrites the attribute in
  `#pragma section(".CRT$XCU", read)` and every constructor fails with C2341.
  Function-like, `read(fd, buf, n)` rewrites `stream->write(stream, …)` in
  `src/stream.h`. Use named helpers.
- **`_close` on an already-closed descriptor fast-fails the process**
  (`0xc0000409`) where POSIX returns `EBADF`. A thread-local invalid-parameter
  handler turns it back into -1.
- **`0xc0000409` has two causes** — a `/GS` overrun, or a CRT argument check.
  Installing an invalid-parameter handler tells them apart: if the fast-fail
  becomes an abort, it was the argument check.
- **munit swallows the stderr of a test that aborts**, because it restores the
  stream only when the test returns. Diagnostics must also write to a file.
- **munit and `/std:c11`.** munit picks VLA-style array parameters on
  `__STDC_VERSION__ >= C99`; MSVC claims C11 and has no VLAs. `CMAKE_C_STANDARD`
  sets the flag on *every* target, so a flag change alone does nothing; `__PGI`
  is munit's own escape hatch. munit must also not get
  `/experimental:c11atomics`, or it takes a `<stdatomic.h>` path MSVC rejects.
- **A missing DLL does not fail fast** on Windows; it raises a loader dialog and
  waits. That hung five doc tests until the timeout.
- **Check the macOS jobs, not just Linux.** GCC merely warns on an implicit
  function declaration; Apple clang rejects it. A dropped `#include
  <execinfo.h>` kept macOS red for many commits while Linux passed.
- **Build locally with `-DENABLE_TESTING_ALL=YES`, and run autotools
  `make check` too.** Plain `ENABLE_TESTING` builds 22 test targets, not 30,
  and `build.yml` runs the autotools suite.
- **`/Zc:preprocessor` does not fix STC.** Its hundred `vec.h` errors were an
  undefined `off_t`.
