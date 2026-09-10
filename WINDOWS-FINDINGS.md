# What stands between libasdf and a Windows (MSVC) build

From the exploratory `windows` job in `.github/workflows/cmake.yml`, run on
`windows-latest` with MSVC 19.51 and vcpkg. **Not for upstream as-is** — the
job exists to answer the question, not to gate anything.

## Correction

An earlier version of this file said libfyaml had no Windows support and was a
hard blocker. **That was wrong.** The grep behind it looked only at
`src/` and `include/` and missed everything: libfyaml ships
`doc/windows-support.txt`, a `cmake/clang-windows-toolchain.cmake`, and a
`CMakeLists.txt` that branches on `WIN32`/`MSVC` throughout.

Vendored and built from source with MSVC, **libfyaml configures and compiles
clean** — configure exit 0, build exit 0, headers and `.lib` installed. With it
in place plus zlib/bzip2/lz4 from vcpkg, **libasdf itself now configures
successfully on Windows.**

## Where it stands

| | |
|---|---|
| libfyaml, vendored + MSVC | **builds clean** |
| zlib 1.3.2, bzip2 1.0.8, lz4 1.10.0 | found via vcpkg + `pkg-config` |
| libasdf configure | **succeeds** |
| libasdf compile | most of `src/` compiles; stops on the two below |

## What is actually left

**1. `ASDF_CONSTRUCTOR` — `include/asdf/util.h`**

```c
/* AFAIK this should be supported on virtually any target/compiler */
#define ASDF_CONSTRUCTOR __attribute__((constructor))
```

MSVC has no `__attribute__`. `src/value_util.c` uses it for
`asdf_common_tag_map_create`/`_destroy` and produces 12 errors. The MSVC
equivalent is a function pointer in `.CRT$XCU` (and `.CRT$XPU` for the
destructor), which the CRT walks before `main` — see `shim.c` in libasdf-rs
for a working spelling. Note the pointer must have **external** linkage or
`/include:` cannot resolve it.

**2. `<sys/time.h>` — `include/asdf/core/time.h`**

Filed as asdf-format/libasdf#261. The header needs only `struct timespec`,
which the `<time.h>` on the next line already provides.

Both are in *public* headers, so they bite anyone vendoring them, not just a
Windows build of libasdf.

## Still ahead, not yet reached

The build stops before these, so they are from reading the source, not from
the compiler:

- `sys/mman.h` — `block.c`, `file.c`, `stream.c`, `core/ndarray.c`,
  `compression/compression.c`. Needs `CreateFileMapping`/`MapViewOfFile` or a
  read-into-buffer fallback. `mmap`/`munmap` appear 18 times.
- `unistd.h` — `file.c`, `parser.c`, `stream.h`, `compression.c`.
- The threaded compression path in `compression.c`: `pthread.h`,
  `sys/eventfd.h`, `sys/syscall.h`, `sys/poll.h`, `sys/ioctl.h`. Largest
  single piece.
- `ssize_t` (9 sites). MSVC has **no** `ssize_t` at all, under any include —
  confirmed directly. `off_t` it does have, behind `<sys/types.h>`.
- `strndup` (12), `timegm` (10), `strptime` (4), `asprintf` (4), `mkstemp`,
  `ftruncate`.
- `argp.h` — `main.c` only, so the CLI can come last.

## Fixed on this branch to get this far

- **`-DUSE_STATGRAB=OFF` did nothing.** The option was declared but never
  consulted, so `pkg_check_modules(STATGRAB libstatgrab REQUIRED)` ran anyway
  and configure died on any platform without libstatgrab. The code already
  treats it as optional — `HAVE_STATGRAB` follows `STATGRAB_FOUND`. Worth
  sending upstream on its own; it is not Windows-specific.
- **`<sys/types.h>` was missing** wherever `off_t`/`ssize_t` are used (13
  files). glibc supplies them transitively, which hides it. On MSVC nothing
  does, so STC's `i_type asdf_block_index, off_t` expanded with an unknown
  type and `vec.h` produced a hundred syntax errors that looked like an STC
  problem and were not. STC compiles fine under MSVC on its own, with either
  preprocessor.

Both verified on Linux: builds clean, 22/22 tests.

## Dead ends, recorded so they are not re-run

- `/Zc:preprocessor` makes no difference. STC's macros are fine under MSVC's
  traditional preprocessor; the problem was the undefined `off_t`.
- MSVC does need `/experimental:c11atomics` for C11 `<stdatomic.h>`
  (`src/error.c` hits it first).
- `cmake/ASDFConfig.cmake` adds `-fvisibility=hidden` and
  `-fmacro-prefix-map=` unconditionally; they reach `cl` as-is and should be
  guarded.

## Reproducing

The `windows` job runs on `workflow_dispatch`. Every step continues on error
so one run reports the whole list.
