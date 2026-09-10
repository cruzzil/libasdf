/* Compatibility shim for endianness conversion functions/macros
 *
 * Needed primarily for macOS which does not have be64toh and friends,
 * though more recent SDKs do seem to include a `sys/endian.h` which
 * provides them.
 */

#pragma once

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Include every endian header that exists, NOT just the first one.  This must
 * match the set of headers used by AX_CHECK_ENDIAN_DECL
 * (m4/ax_check_endian_decl.m4), otherwise the HAVE_DECL_* results can disagree
 * with what is actually visible here.  On macOS, for example, both
 * <machine/endian.h> and <sys/endian.h> *may* exist depending on the SDK
 * version. but the be64toh/htobe* family is declared only in <sys/endian.h>.
 *
 * Thank you Apple for respecting the time and sanity of developers who wish
 * to support your platforms.
 */
#if defined(HAVE_ENDIAN_H)
#include <endian.h>
#endif
#if defined(HAVE_MACHINE_ENDIAN_H)
#include <machine/endian.h>
#endif
#if defined(HAVE_SYS_ENDIAN_H)
#include <sys/endian.h>
#endif

/*
 * Windows is always little-endian on the targets MSVC supports, and the CRT
 * spells the swaps `_byteswap_*`.  Defined before the fallbacks below so their
 * HAVE_DECL_* tests see them.
 */
#if defined(_WIN32)
#include <stdlib.h>
#if !defined(be16toh)
#define be16toh(x) _byteswap_ushort(x)
#define be32toh(x) _byteswap_ulong(x)
#define be64toh(x) _byteswap_uint64(x)
#define htobe16(x) _byteswap_ushort(x)
#define htobe32(x) _byteswap_ulong(x)
#define htobe64(x) _byteswap_uint64(x)
#define le16toh(x) (x)
#define le32toh(x) (x)
#define le64toh(x) (x)
#define htole16(x) (x)
#define htole32(x) (x)
#define htole64(x) (x)
#endif
#undef HAVE_DECL_BE64TOH
#undef HAVE_DECL_BE32TOH
#undef HAVE_DECL_HTOBE16
#undef HAVE_DECL_HTOBE32
#undef HAVE_DECL_HTOBE64
#define HAVE_DECL_BE64TOH 1
#define HAVE_DECL_BE32TOH 1
#define HAVE_DECL_HTOBE16 1
#define HAVE_DECL_HTOBE32 1
#define HAVE_DECL_HTOBE64 1
#endif /* _WIN32 */


#if !HAVE_DECL_BE64TOH
#if (defined(__APPLE__) && !defined(be64toh))
#include <libkern/OSByteOrder.h>
#define be64toh(x) OSSwapBigToHostInt64(x)
#else
#error be64toh not available
#endif
#endif


#if !HAVE_DECL_BE32TOH
#if (defined(__APPLE__) && !defined(be32toh))
#include <libkern/OSByteOrder.h>
#define be32toh(x) OSSwapBigToHostInt32(x)
#else
#error be32toh not available
#endif
#endif


#if !HAVE_DECL_HTOBE16
#if (defined(__APPLE__) && !defined(htobe16))
#include <libkern/OSByteOrder.h>
#define htobe16(x) OSSwapHostToBigInt16(x)
#else
#error htobe16 not available
#endif
#endif


#if !HAVE_DECL_HTOBE32
#if (defined(__APPLE__) && !defined(htobe32))
#include <libkern/OSByteOrder.h>
#define htobe32(x) OSSwapHostToBigInt32(x)
#else
#error htobe32 not available
#endif
#endif


#if !HAVE_DECL_HTOBE64
#if (defined(__APPLE__) && !defined(htobe64))
#include <libkern/OSByteOrder.h>
#define htobe64(x) OSSwapHostToBigInt64(x)
#else
#error htobe64 not available
#endif
#endif


#if !HAVE_DECL_LE32TOH
#if (defined(__APPLE__) && !defined(le32toh))
#include <libkern/OSByteOrder.h>
#define le32toh(x) OSSwapLittleToHostInt32(x)
#else
#error le32toh not available
#endif
#endif


#if !HAVE_DECL_HTOLE32
#if (defined(__APPLE__) && !defined(htole32))
#include <libkern/OSByteOrder.h>
#define htole32(x) OSSwapHostToLittleInt32(x)
#else
#error htole32 not available
#endif
#endif
