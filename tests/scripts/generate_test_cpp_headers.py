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


def main(argv):
    if len(argv) < 4:
        sys.exit("usage: %s <include-dir> <output> <header>..." % argv[0])

    inc_dir, out_path, headers = argv[1], argv[2], argv[3:]
    lines = ["// Auto-generated file. Do not edit."]

    for header in headers:
        rel = os.path.relpath(header, inc_dir).replace(os.sep, "/")
        lines.append("#include <%s>" % rel)

    lines += ["", "int main() {"]

    # One dummy per exported symbol, deduplicated: a symbol may legitimately be
    # declared more than once -- the asdf_open_*_ex family is forward-declared
    # near the top of file.h for the static inline wrappers and declared again
    # in place -- and emitting the same dummy twice is a redeclaration error.
    names = set()

    for header in headers:
        with open(header, encoding="utf-8") as fh:
            for line in fh:
                match = EXPORTED.match(line)
                if match:
                    names.add(match.group(1))

    for name in sorted(names):
        lines.append("    volatile void *dummy_%s = (void *)%s;" % (name, name))

    lines += ["    return 0;", "}", ""]

    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))


if __name__ == "__main__":
    main(sys.argv)
