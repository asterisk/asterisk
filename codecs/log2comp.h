/* log2comp.h - various base 2 log computation versions
 * 
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation by Alex Volkov <codepro@usa.net>
 *
 * Copyright (c) 2004, Digium
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 * Define WANT_ASM before including this file to use assembly
 *   whenever possible
 */

#if defined(_MSC_VER)
#	define inline __inline
#elif defined(__GNUC__)
#	define inline __inline__
#else
#	define inline
#endif

#if defined(WANT_ASM) && defined(_MSC_VER) && defined(_M_IX86)
/* MS C Inline Asm */
#	pragma warning( disable : 4035 )
static inline int log2(int val) { __asm
{
	xor		eax, eax
	dec		eax
	bsr		eax, val
}}
#	pragma warning( default : 4035 )
#elif defined(WANT_ASM) && defined(__GNUC__) && (defined(__i386__) || defined(i386))
/* GNU Inline Asm */
static inline int log2(int val)
{
	int a;
	__asm__
	("\
		xorl	%0, %0		;\
		decl	%0			;\
		bsrl	%1, %0		;\
		"
		: "=r" (a)
		: "mr" (val)
		: "cc"
	);
	return a;
}
#else
/* no ASM for this compiler and/or platform */
/* rather slow base 2 log computation
 * Using looped shift.
 */
static inline int log2(int val)
{
	int i;
	for (i = -1; val; ++i, val >>= 1)
		;
	return (i);
}
#endif
