# Generates test-cpp-headers.cpp, which checks the public headers compile and
# link as C++.
#
# CMake script rather than the shell script this replaces: it is invoked with
# `cmake -P`, so it needs no `sh` and works on Windows, where the build
# otherwise failed with "test-cpp-headers.cpp: No such file or directory".
#
# Usage:
#   cmake -DINC_DIR=<dir> -DOUT=<file> -DHEADERS=<;-list> -P <this script>

set(lines "// Auto-generated file. Do not edit.\n")

foreach(header IN LISTS HEADERS)
    # Strip the include root to get <asdf/...>.
    file(RELATIVE_PATH rel "${INC_DIR}" "${header}")
    string(APPEND lines "#include <${rel}>\n")
endforeach()

string(APPEND lines "\nint main() {\n")

# One dummy per ASDF_EXPORT symbol, deduplicated: a symbol may legitimately be
# declared more than once -- the asdf_open_*_ex family is forward-declared near
# the top of file.h for the static inline wrappers and declared again in place
# -- and emitting the same dummy twice is a redeclaration error.
set(dummies "")

foreach(header IN LISTS HEADERS)
    file(STRINGS "${header}" exported REGEX "^ASDF_EXPORT.*\\(")

    foreach(decl IN LISTS exported)
        if(decl MATCHES "^ASDF_EXPORT[ \t]+[^()]+[ \t]+\\*?([A-Za-z_][A-Za-z0-9_]*)[ \t]*\\(")
            # No trailing `;` in the element: CMake would read it as a list
            # separator and drop it. It is added when the line is written.
            list(APPEND dummies
                "    volatile void *dummy_${CMAKE_MATCH_1} = (void *)${CMAKE_MATCH_1}")
        endif()
    endforeach()
endforeach()

list(REMOVE_DUPLICATES dummies)
list(SORT dummies)

foreach(dummy IN LISTS dummies)
    string(APPEND lines "${dummy};\n")
endforeach()

string(APPEND lines "    return 0;\n}\n")

file(WRITE "${OUT}" "${lines}")
