/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 * \brief Compiler-specific macros and other items
 */

#ifndef _ASTERISK_COMPILER_H
#define _ASTERISK_COMPILER_H

#ifdef HAVE_ATTRIBUTE_always_inline
#define force_inline __attribute__((always_inline)) inline
#else
#define force_inline inline
#endif

#ifdef HAVE_ATTRIBUTE_pure
#define attribute_pure __attribute__((pure))
#else
#define attribute_pure
#endif

#ifdef HAVE_ATTRIBUTE_const
#define attribute_const __attribute__((const))
#else
#define attribute_const
#endif

#ifdef HAVE_ATTRIBUTE_deprecated
#define attribute_deprecated __attribute__((deprecated))
#else
#define attribute_deprecated
#endif

#ifdef HAVE_ATTRIBUTE_unused
#define attribute_unused __attribute__((unused))
#else
#define attribute_unused
#endif

#ifdef HAVE_ATTRIBUTE_malloc
#define attribute_malloc __attribute__((malloc))
#else
#define attribute_malloc
#endif

#ifdef HAVE_ATTRIBUTE_sentinel
#define attribute_sentinel __attribute__((sentinel))
#else
#define attribute_sentinel
#endif

#ifdef HAVE_ATTRIBUTE_warn_unused_result
#define attribute_warn_unused_result __attribute__((warn_unused_result))
#else
#define attribute_warn_unused_result
#endif

#ifdef HAVE_ATTRIBUTE_may_alias
#define attribute_may_alias __attribute__((may_alias))
#else
#define attribute_may_alias
#endif

#ifdef HAVE_ATTRIBUTE_noreturn
#define attribute_noreturn __attribute__((noreturn))
#else
#define attribute_noreturn
#endif

/* Some older version of GNU gcc (3.3.5 on OpenBSD 4.3 for example) dont like 'NULL' as sentinel */
#define SENTINEL ((char *)NULL)

#endif /* _ASTERISK_COMPILER_H */
