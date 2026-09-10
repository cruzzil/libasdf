#!/bin/sh
# Generates test_cpp_headers.cpp for testing compilation/linking in C++

INC_DIR="$1"
OUT="$2"
shift 2  # Remaining arguments are header files to process

echo "// Auto-generated file. Do not edit." > "$OUT"

# Automatically generate #include lines for all listed headers
for header in "$@"; do
    # Strip the leading ../include/ to get <asdf/...>
    include_path=$(echo "$header" | sed "s|$INC_DIR/|<|; s|$|>|")
    echo "#include $include_path" >> "$OUT"
done

echo "" >> "$OUT"
echo "int main() {" >> "$OUT"

# Loop through all .h files and extract ASDF_EXPORT symbols.
#
# Deduplicated: a symbol may legitimately be declared more than once -- the
# asdf_open_*_ex family is forward-declared near the top of file.h for the
# static inline wrappers and declared again in place -- and emitting the same
# dummy twice is a redeclaration error.
for header in "$@"; do
    grep '^ASDF_EXPORT.*(' "$header" | \
    sed -E 's/ASDF_EXPORT[[:space:]]+[^()]*[[:space:]]+\*?([a-zA-Z_][a-zA-Z0-9_]*)\(.*/    volatile void *dummy_\1 = (void *)\1;/'
done | sort -u >> "$OUT"

echo "    return 0;" >> "$OUT"
echo "}" >> "$OUT"
