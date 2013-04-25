/*      $NetBSD: bswap.h,v 1.16 2009/08/08 21:23:15 christos Exp $      */

/* Written by Manuel Bouyer. Public domain */

#ifndef _SYS_BSWAP_H_
#define _SYS_BSWAP_H_

#ifndef _LOCORE
#include <sys/cdefs.h>
#include <sys/types.h>

#include <machine/bswap.h>

__BEGIN_DECLS

/* Android-changed: rename bswap* to __bswap*. */

/* Always declare the functions in case their address is taken (etc) */
#if defined(_KERNEL) || defined(_STANDALONE) || !defined(__BSWAP_RENAME)
uint16_t __bswap16(uint16_t) __constfunc;
uint32_t __bswap32(uint32_t) __constfunc;
#else
uint16_t __bswap16(uint16_t) __RENAME(__bswap16) __constfunc;
uint32_t __bswap32(uint32_t) __RENAME(__bswap32) __constfunc;
#endif
uint64_t __bswap64(uint64_t) __constfunc;
__END_DECLS

/* Android-changed: In case of __bswap64 inline version absence,
 * we should take the NetBSD version of __nbcompat_bswap64 for
 * __bswap64
 */
#define __nbcompat_bswap64(x)   (((u_int64_t)__bswap32((x)) << 32) | \
				 ((u_int64_t)__bswap32((x) >> 32)))
#ifndef __bswap64
#define __bswap64 __nbcompat_bswap64
#endif

#if defined(__GNUC__) && defined(__OPTIMIZE__) && !defined(__lint__)

/* machine/byte_swap.h might have defined inline versions */

#ifndef __BYTE_SWAP_U64_VARIABLE

#define	__BYTE_SWAP_U64_VARIABLE __bswap64
#endif

#ifndef __BYTE_SWAP_U32_VARIABLE
#define	__BYTE_SWAP_U32_VARIABLE __bswap32
#endif

#ifndef __BYTE_SWAP_U16_VARIABLE
#define	__BYTE_SWAP_U16_VARIABLE __bswap16
#endif

#define	__byte_swap_u64_constant(x) \
	(__CAST(uint64_t, \
	 ((((x) & 0xff00000000000000ull) >> 56) | \
	  (((x) & 0x00ff000000000000ull) >> 40) | \
	  (((x) & 0x0000ff0000000000ull) >> 24) | \
	  (((x) & 0x000000ff00000000ull) >>  8) | \
	  (((x) & 0x00000000ff000000ull) <<  8) | \
	  (((x) & 0x0000000000ff0000ull) << 24) | \
	  (((x) & 0x000000000000ff00ull) << 40) | \
	  (((x) & 0x00000000000000ffull) << 56))))

#define	__byte_swap_u32_constant(x) \
	(__CAST(uint32_t, \
	((((x) & 0xff000000) >> 24) | \
	 (((x) & 0x00ff0000) >>  8) | \
	 (((x) & 0x0000ff00) <<  8) | \
	 (((x) & 0x000000ff) << 24))))

#define	__byte_swap_u16_constant(x) \
	(__CAST(uint16_t, \
	((((x) & 0xff00) >> 8) | \
	 (((x) & 0x00ff) << 8))))

#define	__bswap64(x) \
	(__builtin_constant_p((x)) ? \
	 __byte_swap_u64_constant(x) : __BYTE_SWAP_U64_VARIABLE(x))

#define	__bswap32(x) \
	(__builtin_constant_p((x)) ? \
	 __byte_swap_u32_constant(x) : __BYTE_SWAP_U32_VARIABLE(x))

#define	__bswap16(x) \
	(__builtin_constant_p((x)) ? \
	 __byte_swap_u16_constant(x) : __BYTE_SWAP_U16_VARIABLE(x))

#endif /* __GNUC__ && __OPTIMIZE__ */
#endif /* !_LOCORE */

#endif /* !_SYS_BSWAP_H_ */
