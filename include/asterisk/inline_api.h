/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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

#ifndef __ASTERISK_INLINEAPI_H
#define __ASTERISK_INLINEAPI_H

/*! \file
 * \brief Inlinable API function macro

  Small API functions that are candidates for inlining need to be specially
  declared and defined, to ensure that the 'right thing' always happens.
  For example:
	- there must _always_ be a non-inlined version of the function
	available for modules compiled out of the tree to link to
	- references to a function that cannot be inlined (for any
	reason that the compiler deems proper) must devolve into an
	'extern' reference, instead of 'static', so that multiple
	copies of the function body are not built in different modules.
	However, since this doesn't work for clang, we go with 'static'
	anyway and hope for the best!
	- when DISABLE_INLINE is defined, inlining should be disabled
	completely, even if the compiler is configured to support it

  The AST_INLINE_API macro allows this to happen automatically, when
  used to define your function. Proper usage is as follows:
  - define your function one place, in a header file, using the macro
  to wrap the function (see strings.h or time.h for examples)
  - choose a module to 'host' the function body for non-inline
  usages, and in that module _only_, define AST_API_MODULE before
  including the header file
 */

#if !defined(DISABLE_INLINE)

#if !defined(AST_API_MODULE)
#if defined(__clang__) || defined(__GNUC_STDC_INLINE__)
#define AST_INLINE_API(hdr, body) static hdr; static inline hdr body
#else /* if defined(__clang__) */
#define AST_INLINE_API(hdr, body) hdr; extern inline hdr body
#endif
#else /* if !defined(AST_API_MODULE) */
#define AST_INLINE_API(hdr, body) hdr; hdr body
#endif

#else /* defined(DISABLE_INLINE) */

#if !defined(AST_API_MODULE)
#define AST_INLINE_API(hdr, body) hdr;
#else
#define AST_INLINE_API(hdr, body) hdr; hdr body
#endif

#endif

#undef AST_API_MODULE

#endif /* __ASTERISK_INLINEAPI_H */
