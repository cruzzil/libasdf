# libasdf on Windows (MSVC)

From the exploratory `windows` job in `.github/workflows/cmake.yml`
(`workflow_dispatch`, `windows-latest`, MSVC 19.51, vcpkg). **Private branch,
not for upstream.**

## Where it stands

Configure **succeeds**. Of 33 files in `src/`, all but a handful compile.

| | |
|---|---|
| libfyaml, vendored + built with MSVC | clean |
| zlib 1.3.2, bzip2 1.0.8, lz4 1.10.0 | vcpkg + `pkg-config` |
| libasdf configure | succeeds |
| libasdf compile | 5 files left, listed below |

An earlier version of this file claimed libfyaml had no Windows support and
was a hard blocker. **That was wrong** — the grep behind it looked only at
`src/` and `include/`. libfyaml ships `doc/windows-support.txt`, a
`clang-windows-toolchain.cmake`, and a `CMakeLists.txt` that branches on
`WIN32`/`MSVC` throughout, and it builds clean as a vendored subproject.

## What is left

**1. `read` / `write` / `close` — the interesting one**

`asdf_stream` has members of those names, so they cannot be macro-shimmed:
any macro fires on `stream->close(stream)`. `#define read _read` also
rewrote the `read` attribute inside `#pragma section(".CRT$XCU", read)`,
which is what made every constructor fail with C2341 for several rounds.

The fd call sites need a neutral spelling (`asdf_read` and friends, resolved
per platform). That is a source change, not a shim, and it is the next piece
of real work.

**2. Non-constant static initializers** — `file.c:32`, `emitter.c:23`

`asdf_config_default` and `asdf_emitter_cfg_default` are `static const`
aggregates whose initializers MSVC rejects (C2099, C2078). Needs a look at
what is non-constant in them.

**3. `core/time.c` `JD_*_EPOCH` undeclared** (6 sites)

Almost certainly behind a `HAVE_*` that is off on this platform.

**4. `compression/compression.c`** — still the largest piece

`sys/mman.h` plus `pthread.h`, `sys/eventfd.h`, `sys/syscall.h`,
`sys/poll.h`, `sys/ioctl.h`. The asynchronous decompression path wants a
design (a Windows thread pool, or a serial fallback), not a shim. Left with
its own POSIX includes on purpose.

**5. The CLI** — `main.c` needs an `argp` replacement. Last.

## Done on this branch

- **`src/compat/posix.h`** — `<unistd.h>`/`<sys/mman.h>` stand-in. Read-only
  file mapping via `CreateFileMapping`/`MapViewOfFile`; `munmap` asks
  `VirtualQuery` whether it holds a view or an anonymous allocation, since
  the caller does not say and the two are released differently. `ssize_t` and
  `SSIZE_MAX` live here too — MSVC has neither, under any include.
- **`ASDF_CONSTRUCTOR`/`ASDF_DESTRUCTOR`** now take the function name and
  stand in for the whole declarator, so one spelling covers
  `__attribute__((constructor))` and MSVC's `.CRT$XCU`. The extra macro
  indirection is needed because `ASDF_REGISTER_EXTENSION` passes a name that
  is itself an expansion. Destructors go through `atexit`.
- **`UNUSED(x)`** — its non-GCC expansion was `(void)(x)`, a syntax error in
  the parameter position it is used in.
- **`<sys/types.h>`** added wherever `off_t`/`ssize_t` are used (13 files).
  glibc supplies them transitively, which hid it; on MSVC the undefined
  `off_t` made STC's `i_type asdf_block_index, off_t` expand with an unknown
  type and produce a hundred `vec.h` errors that looked like an STC problem.
- **`compat/endian.h`** gained a Windows branch (`_byteswap_*`).
- **`void *` arithmetic** replaced with `char *` in `stream.c`, `util.c`,
  `core/ndarray.c` — a GCC extension MSVC does not have.
- **The VLA in `parse_util.c`** is now a fixed array sized by `ASDF_LAST_TOK`,
  which already bounds it. Heap was the obvious first move and the wrong one:
  `test-malloc-fail` injects failure at a counted allocation, so two more
  `calloc`s shifted the count and broke it.
- **`-DUSE_STATGRAB=OFF`** now works — sent upstream separately as
  asdf-format/libasdf#262 / #263, since it is not Windows-specific.
- **`<sys/time.h>`** dropped from `core/time.h` — asdf-format/libasdf#261.

Every one of these is verified on Linux: builds clean, 22/22 tests.

## Dead ends, recorded so they are not re-run

- `/Zc:preprocessor` changes nothing. STC compiles under MSVC's traditional
  preprocessor either way; the problem was the undefined `off_t`.
- `#pragma section(".CRT$XCU", long, read)` — `long` is not a valid attribute.
- MSVC does need `/experimental:c11atomics` for C11 `<stdatomic.h>`
  (`src/error.c` hits it first).
- `cmake/ASDFConfig.cmake` adds `-fvisibility=hidden` and
  `-fmacro-prefix-map=` unconditionally; they reach `cl` as-is.
- A `tee | tail` in the CI build step let `bash -e -o pipefail` kill the step
  before it classified anything, so several rounds were read from the last 20
  lines of the log rather than the whole of it. Capture to a file first.
