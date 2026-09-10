#ifndef ASDF_UTIL_H
#define ASDF_UTIL_H

#include <asdf/config.h> // IWYU pragma: export

#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 4)
#define ASDF_EXPORT __attribute__((visibility("default")))
#define ASDF_LOCAL __attribute__((visibility("hidden")))
#else
#define ASDF_EXPORT
#define ASDF_LOCAL
#endif


#ifdef __cplusplus
#define ASDF_BEGIN_DECLS extern "C" {
#define ASDF_END_DECLS }
#define ASDF_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define ASDF_BEGIN_DECLS
#define ASDF_END_DECLS
#define ASDF_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif


/*
 * Run a function before `main`, or at exit.
 *
 * Both take the function's name and stand in for its whole declarator:
 *
 *     ASDF_CONSTRUCTOR(my_init) { ... }
 *
 * GCC and Clang have `__attribute__((constructor))`, which could be a bare
 * prefix.  MSVC's equivalent is a function pointer in a section the CRT walks
 * before `main`, and that has to be emitted as a separate object beside the
 * function -- so a prefix cannot express it, and taking the name is what lets
 * one spelling cover both.
 */
#if defined(_MSC_VER)

#include <stdlib.h> /* atexit */

#pragma section(".CRT$XCU", read)

/*
 * The indirection is what makes these usable on a name that is itself a
 * macro expansion, as `ASDF_REGISTER_EXTENSION` needs: `#` and `##` would
 * otherwise see the unexpanded text.
 */
#define ASDF_CONSTRUCTOR(f) ASDF__CONSTRUCTOR_I(f)
#define ASDF_DESTRUCTOR(f) ASDF__DESTRUCTOR_I(f)

/*
 * The pointer has external linkage deliberately: `/include:` asks the linker
 * to resolve a symbol, and a `static` one is invisible to it, so the
 * reference goes unresolved.  Nothing is `dllexport`, so it stays out of the
 * export table.  x86 decorates the name with a leading underscore; x64 and
 * ARM64 do not.
 */
#if defined(_WIN64)
#define ASDF__CTOR_INCLUDE(f) "/include:" #f "_asdf_ctor"
#else
#define ASDF__CTOR_INCLUDE(f) "/include:_" #f "_asdf_ctor"
#endif

#define ASDF__CONSTRUCTOR_I(f) \
    static void f(void); \
    __declspec(allocate(".CRT$XCU")) void (*f##_asdf_ctor)(void) = f; \
    __pragma(comment(linker, ASDF__CTOR_INCLUDE(f))) \
    static void f(void)

/* MSVC has no destructor section; the CRT runs `atexit` handlers instead. */
#define ASDF__DESTRUCTOR_I(f) \
    static void f(void); \
    ASDF__CONSTRUCTOR_I(f##_asdf_atexit) { atexit(f); } \
    static void f(void)

#else

#define ASDF_CONSTRUCTOR(f) __attribute__((constructor)) static void f(void)
#define ASDF_DESTRUCTOR(f) __attribute__((destructor)) static void f(void)

#endif


#endif /* ASDF_UTIL_H */
