# What stands between libasdf and a Windows (MSVC) build

Findings from the exploratory `windows` job in `.github/workflows/cmake.yml`,
run on `windows-latest` with MSVC 19.51 and vcpkg. **Not for upstream as-is** —
the job exists to answer the question, not to gate anything.

## Short version

One hard blocker, then a tractable porting job. The dependencies people assume
are the problem (zlib, bzip2, lz4) are not: vcpkg has all three and the
existing detection finds them unchanged once `pkg-config` is on the runner.

## The blocker: libfyaml

`pkg_check_modules(FYAML libfyaml REQUIRED)` fails, and configure stops there.
libfyaml is the YAML engine — `src/util.h` and `src/file.h` include
`libfyaml.h`, so it is on the include path of essentially every translation
unit, and nothing compiles without it.

It is not in vcpkg. It ships a `CMakeLists.txt`, so it is not autotools-only,
but a search of its `src/` and `include/` for `_WIN32`, `MSVC` or `MinGW`
returns **nothing**: there is no Windows support in it to build against.

So a Windows port of libasdf needs a Windows port of libfyaml first, or a
second YAML backend behind the `asdf_yaml_*` seam.

## Dependencies, once pkg-config exists

| | |
|---|---|
| BZip2 1.0.8 | found (vcpkg) |
| liblz4 1.10.0 | found (vcpkg) |
| zlib 1.3.2 | found (vcpkg) |
| libfyaml | **not available** |
| libmd (`md5.h`) | not in vcpkg; needs a bundled MD5 or a CryptoAPI path |
| libstatgrab | not in vcpkg; `USE_STATGRAB=OFF` already exists |
| argp | not in vcpkg; only `src/main.c` (the CLI) needs it |

The runner has no `pkg-config`; `choco install pkgconfiglite` supplies one and
vcpkg ships `.pc` files, so no build-system change is needed for these.

## Toolchain

MSVC rejects C11 `<stdatomic.h>` unless it is asked to:
`vcruntime_c11_stdatomic.h(12): error C1189: "C atomic support is not enabled"`.
`/experimental:c11atomics /std:c11` clears it. `src/error.c` is the first file
to hit it.

## POSIX in the source

| Header | Files |
|---|---|
| `sys/mman.h` | `block.c`, `compression/compression.c`, `core/ndarray.c`, `file.c`, `stream.c` |
| `unistd.h` | `compression/compression.c`, `file.c`, `parser.c`, `stream.h` |
| `pthread.h`, `sys/eventfd.h`, `sys/syscall.h`, `sys/poll.h`, `sys/ioctl.h` | `compression/compression.c` |
| `endian.h` | `compat/endian.h` (already probed, has fallbacks) |
| `argp.h` | `main.c` (CLI only) |
| `sys/time.h` | `include/asdf/core/time.h` — gratuitous: it needs only `struct timespec`, which `<time.h>` provides |

Functions and types: `off_t` (53), `mmap`/`munmap` (18), `strndup` (12),
`timegm` (10), `ssize_t` (9), `strptime` (4), `asprintf` (4), `mkstemp`,
`ftruncate`.

Two clusters do most of the work: memory-mapped reading, which needs
`CreateFileMapping`/`MapViewOfFile` or a read-into-buffer fallback, and the
threaded compression path in `compression.c`, which is built on eventfd,
`poll` and raw syscalls.

## Order of work, if it is wanted

1. Get libfyaml building on Windows, or put a second backend behind the YAML
   seam. Nothing else matters until this is settled.
2. `/experimental:c11atomics`, and the `sys/time.h` include dropped from
   `core/time.h` — a one-line change that also unblocks anyone else vendoring
   the public headers.
3. An mmap shim, or a non-mmap read path.
4. `strndup`, `asprintf`, `timegm`, `strptime`, `mkstemp`, `ftruncate` shims;
   `off_t`/`ssize_t` to fixed-width types in internal signatures.
5. The compression thread pool, which is the largest single piece.
6. The CLI last: it needs an `argp` replacement of its own.

## Reproducing

The `windows` job runs on `workflow_dispatch`. Every step continues on error
so one run reports the whole list, and the final step surveys the source even
when configure never gets far enough to produce output.
