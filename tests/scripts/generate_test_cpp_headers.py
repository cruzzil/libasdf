#!/usr/bin/env python3
"""Generate test-cpp-headers.cpp, which checks the public headers compile and
link as C++.

Python rather than the shell script this replaces: both build systems already
require Python, and `sh` is not available on Windows, where the build failed
on a missing test-cpp-headers.cpp.

Usage: generate_test_cpp_headers.py <include-dir> <output> <header>...
"""

import os
import re
import sys

# `ASDF_EXPORT <return type> [*]name(`, allowing the return type to wrap.
EXPORTED = re.compile(r"^ASDF_EXPORT\s+[^()]+?\s+\*?([A-Za-z_][A-Za-z0-9_]*)\s*\(")
IFNDEF = re.compile(r"^#\s*ifndef\s+([A-Za-z_][A-Za-z0-9_]*)")
DEFINE = re.compile(r"^#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)")


def scan(path):
    """Yield (name, conditions) for each exported symbol in one header.

    `conditions` is the stack of preprocessor conditionals the declaration sits
    inside, so the dummy can be emitted under the same ones.  Not everything
    is declared unconditionally -- asdf_ndarray_read_float16_at exists only
    where ASDF_HAVE_FLOAT16 does, and MSVC has no _Float16 -- and a dummy for a
    symbol that was not declared does not compile.

    Include guards are skipped: the guard macro is defined by the time the
    generated file is compiled, so honouring it would drop every symbol.
    """
    with open(path, encoding="utf-8") as fh:
        lines = fh.readlines()

    stack = []

    for idx, line in enumerate(lines):
        stripped = line.strip()

        if stripped.startswith("#if"):
            guard = IFNDEF.match(stripped)

            if guard:
                # The classic `#ifndef X` / `#define X` pair.
                nxt = next((l.strip() for l in lines[idx + 1:] if l.strip()), "")
                define = DEFINE.match(nxt)

                if define and define.group(1) == guard.group(1):
                    stack.append(None)
                    continue

            stack.append(stripped)
            continue

        if stripped.startswith("#endif"):
            if stack:
                stack.pop()
            continue

        match = EXPORTED.match(line)

        if match:
            yield match.group(1), tuple(c for c in stack if c is not None)


def main(argv):
    if len(argv) < 4:
        sys.exit("usage: %s <include-dir> <output> <header>..." % argv[0])

    inc_dir, out_path, headers = argv[1], argv[2], argv[3:]
    lines = ["// Auto-generated file. Do not edit."]

    for header in headers:
        rel = os.path.relpath(header, inc_dir).replace(os.sep, "/")
        lines.append("#include <%s>" % rel)

    lines += ["", "int main() {"]

    # Deduplicated: a symbol may legitimately be declared more than once -- the
    # asdf_open_*_ex family is forward-declared near the top of file.h for the
    # static inline wrappers and declared again in place -- and emitting the
    # same dummy twice is a redeclaration error.
    seen = {}

    for header in headers:
        for name, conditions in scan(header):
            # An unconditional declaration wins: the symbol is always there.
            if name not in seen or not conditions:
                seen[name] = conditions

    for name in sorted(seen):
        conditions = seen[name]

        for condition in conditions:
            lines.append(condition)

        lines.append("    volatile void *dummy_%s = (void *)%s;" % (name, name))
        lines.extend(["#endif"] * len(conditions))

    lines += ["    return 0;", "}", ""]

    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))


if __name__ == "__main__":
    main(sys.argv)
