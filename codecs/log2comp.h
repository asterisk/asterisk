/*! \file
 * \brief log2comp.h - various base 2 log computation versions
 *
 * Asterisk -- An open source telephony toolkit.
 *
 * \author Alex Volkov <codepro@usa.net>
 *
 * Copyright (c) 2004 - 2005, Digium Inc.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 * __builtin_clz covers all GCC/Clang targets (x86, x86_64, ARM, AArch64,
 * RISC-V, PowerPC, ...) in a single path and lets the compiler emit LZCNT
 * on Haswell+ / Zen1+ targets.
 */

#if defined(__GNUC__) || defined(__clang__)
static inline int ilog2(int val)
{
	if (val <= 0) {
		return -1;
	}
	return 31 - __builtin_clz((unsigned int)val);
}
#elif defined(_MSC_VER)
#include <intrin.h>
static __inline int ilog2(int val)
{
	unsigned long res;
	return (val > 0 && _BitScanReverse(&res, (unsigned long)val)) ? (int)res : -1;
}
#else
/* Portable fallback: O(log2(val)) shift loop */
static inline int ilog2(int val)
{
	int i;
	for (i = -1; val > 0; ++i, val >>= 1)
		;
	return i;
}
#endif
