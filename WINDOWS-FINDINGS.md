# libasdf on Windows (MSVC)

**It builds, links, exports, and runs.**

```
using import library: D:\a\libasdf\libasdf\build\src\RelWithDebInfo\asdf.lib
name=Dennis Ritchie answer=42
SMOKE OK
---- smoke exit 0
```

That is a real program compiled against the vendored headers and the import
library, writing an ASDF file and reading the values back, on `windows-latest`
with MSVC 19.51. The `windows` job in `.github/workflows/cmake.yml` does it on
`workflow_dispatch`. **Private branch, not for upstream.**

Linux and macOS are unaffected: gcc and clang, with and without ASan, all green
at 30/30 tests.

## The shape of it

Nothing needed rewriting. It came down to a compat header, a handful of
over-broad `#ifdef`s, and the fact that Windows exports no symbols unless you
say so. libfyaml -- which an earlier version of this file wrongly called a hard
blocker -- builds clean with MSVC as a vendored subproject, and zlib, bzip2 and
lz4 come from vcpkg with the existing detection unchanged.

## What still does not build

Both third-party, neither in libasdf:

- **The tests.** munit is built on VLAs, which MSVC does not implement at any
  `/std:` level. Needs munit patches or a different framework.
- **The CLI.** `main.c` wants `argp`, which has no Windows port.

Configured with `-DENABLE_TESTING_ALL=NO -DENABLE_TOOL=OFF` for that reason.

## What was needed

**Exports.** `ASDF_EXPORT` was empty on MSVC, so the DLL had no export table
and no import library -- nothing could link against it. It now expands to
`__declspec(dllexport)` while the library is built and `dllimport` for callers,
selected by `ASDF_BUILDING_DLL`. The `asdf_open_*_ex` forward declarations in
`file.h` had to be marked too: GCC merges attributes across declarations,
MSVC calls a bare one a redefinition with different linkage.

**`/Zc:preprocessor` is required for consumers, not just for the build.**
`asdf_open` and `asdf_write_to` are `_Generic` macros dispatched on a
`__VA_ARGS__` argument count; MSVC's traditional preprocessor counts it wrong
and silently picks the `FILE *` overload for a filename. Also
`/experimental:c11atomics` for C11 `<stdatomic.h>`.

**`src/compat/posix.h`** -- `<unistd.h>` and `<sys/mman.h>` on Win32: file
mapping via `CreateFileMapping`/`MapViewOfFile`, with `munmap` asking
`VirtualQuery` whether it holds a view or an anonymous allocation, since the
caller does not say and the two are released differently. Plus `ssize_t`,
`SSIZE_MAX`, `PATH_MAX`, `strndup`, `strcasecmp`, `asprintf`, `fseeko`/`ftello`,
`timegm`, `mkstemp`, and `open_memstream` -- the last on a temp file, handing
the contents back when the stream closes, which is exactly what the one caller
does.

**`ASDF_CONSTRUCTOR`/`ASDF_DESTRUCTOR`** now take the function name and stand
in for the whole declarator, so one spelling covers
`__attribute__((constructor))` and MSVC's `.CRT$XCU`. Destructors go through
`atexit`.

**Two over-broad `#ifdef HAVE_STRPTIME` guards** -- these are latent bugs, not
Windows ones. The Julian Date constants and then thirteen arithmetic time
parsers were inside a block guarding code that needs `strptime`, while the
format dispatch outside called them unconditionally. On any platform without
`strptime` the file failed to compile, then failed to link. Reproducible on
Linux by clearing `HAVE_STRPTIME` in `config.h`.

**`-DUSE_STATGRAB=OFF` did nothing** -- also not Windows-specific, and sent
upstream separately as asdf-format/libasdf#262 / #263.

**Smaller things:** `<sys/types.h>` missing wherever `off_t` is used (13
files); a Windows branch in `compat/endian.h`; `__builtin_bswap*` →
`_byteswap_*`; `void *` arithmetic → `char *`; the VLA in `parse_util.c` → a
fixed array sized by the bound it already had; `UNUSED(x)`, whose non-GCC
expansion `(void)(x)` is a syntax error in the parameter position it is used
in; `<sys/time.h>` dropped from `core/time.h` (asdf-format/libasdf#261); and
libm not linked on Windows, where the CRT carries the math functions.

## Traps worth remembering

- **Do not macro-define `read`, `write` or `close`.** `asdf_stream` has members
  of those names, so any macro fires on `stream->close(stream)`. `#define read
  _read` also rewrote the `read` attribute inside `#pragma section(".CRT$XCU",
  read)`, which made every constructor fail with C2341 and cost several rounds
  of misdiagnosis. Named helpers (`asdf_close_fd`) instead.
- **`/Zc:preprocessor` does not fix STC.** STC compiles under either
  preprocessor; the hundred `vec.h` errors were an undefined `off_t` reaching
  `i_type asdf_block_index, off_t`.
- **`long` is not a valid `#pragma section` attribute.**
- **Build locally with `-DENABLE_TESTING_ALL=YES`.** Plain `ENABLE_TESTING`
  builds 22 targets; CI builds 30, and the generated C++ header test is among
  the eight it adds. A duplicate-symbol regression in it went unnoticed here
  for exactly that reason.
- `cmake/ASDFConfig.cmake` adds `-fvisibility=hidden` and
  `-fmacro-prefix-map=` unconditionally; they reach `cl` as-is and should be
  guarded.
