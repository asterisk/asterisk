/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
 *
 * Tilghman Lesher <tlesher@digium.com>
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
 *
 * \brief String manipulation API
 *
 * \author Tilghman Lesher <tilghman@digium.com>
 */

/*** MAKEOPTS
<category name="MENUSELECT_CFLAGS" displayname="Compiler Flags" positive_output="yes" remove_on_change=".lastclean">
	<member name="DEBUG_OPAQUE" displayname="Change ast_str internals to detect improper usage">
		<defaultenabled>yes</defaultenabled>
	</member>
</category>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/strings.h"
#include "asterisk/pbx.h"

/*!
 * core handler for dynamic strings.
 * This is not meant to be called directly, but rather through the
 * various wrapper macros
 *	ast_str_set(...)
 *	ast_str_append(...)
 *	ast_str_set_va(...)
 *	ast_str_append_va(...)
 */

int __ast_str_helper(struct ast_str **buf, size_t max_len,
	int append, const char *fmt, va_list ap)
{
	int res, need;
#ifdef DEBUG_OPAQUE
	int offset = (append && (*buf)->len2) ? (*buf)->used2 : 0;
#else
	int offset = (append && (*buf)->len) ? (*buf)->used : 0;
#endif
	va_list aq;

	do {
		if (max_len < 0) {
#ifdef DEBUG_OPAQUE
			max_len = (*buf)->len2;	/* don't exceed the allocated space */
#else
			max_len = (*buf)->len;	/* don't exceed the allocated space */
#endif
		}
		/*
		 * Ask vsnprintf how much space we need. Remember that vsnprintf
		 * does not count the final '\0' so we must add 1.
		 */
		va_copy(aq, ap);
#ifdef DEBUG_OPAQUE
		res = vsnprintf((*buf)->str2 + offset, (*buf)->len2 - offset, fmt, aq);
#else
		res = vsnprintf((*buf)->str + offset, (*buf)->len - offset, fmt, aq);
#endif

		need = res + offset + 1;
		/*
		 * If there is not enough space and we are below the max length,
		 * reallocate the buffer and return a message telling to retry.
		 */
#ifdef DEBUG_OPAQUE
		if (need > (*buf)->len2 && (max_len == 0 || (*buf)->len2 < max_len) ) {
#else
		if (need > (*buf)->len && (max_len == 0 || (*buf)->len < max_len) ) {
#endif
			if (max_len && max_len < need) {	/* truncate as needed */
				need = max_len;
			} else if (max_len == 0) {	/* if unbounded, give more room for next time */
				need += 16 + need / 4;
			}
			if (0) {	/* debugging */
#ifdef DEBUG_OPAQUE
				ast_verbose("extend from %d to %d\n", (int)(*buf)->len2, need);
#else
				ast_verbose("extend from %d to %d\n", (int)(*buf)->len, need);
#endif
			}
			if (ast_str_make_space(buf, need)) {
#ifdef DEBUG_OPAQUE
				ast_verbose("failed to extend from %d to %d\n", (int)(*buf)->len2, need);
#else
				ast_verbose("failed to extend from %d to %d\n", (int)(*buf)->len, need);
#endif
				return AST_DYNSTR_BUILD_FAILED;
			}
#ifdef DEBUG_OPAQUE
			(*buf)->str2[offset] = '\0';	/* Truncate the partial write. */
#else
			(*buf)->str[offset] = '\0';	/* Truncate the partial write. */
#endif

			/* Restart va_copy before calling vsnprintf() again. */
			va_end(aq);
			continue;
		}
		break;
	} while (1);
	/* update space used, keep in mind the truncation */
#ifdef DEBUG_OPAQUE
	(*buf)->used2 = (res + offset > (*buf)->len2) ? (*buf)->len2 : res + offset;
#else
	(*buf)->used = (res + offset > (*buf)->len) ? (*buf)->len : res + offset;
#endif

	return res;
}

void ast_str_substitute_variables(struct ast_str **buf, size_t maxlen, struct ast_channel *chan, const char *template)
{
	int first = 1;
#ifdef DEBUG_OPAQUE
	do {
		ast_str_make_space(buf, maxlen ? maxlen :
			(first ? strlen(template) * 2 : (*buf)->len2 * 2));
		pbx_substitute_variables_helper_full(chan, NULL, template, (*buf)->str2, (*buf)->len2 - 1, &((*buf)->used2));
		first = 0;
	} while (maxlen == 0 && (*buf)->len2 - 5 < (*buf)->used2);
#else
	do {
		ast_str_make_space(buf, maxlen ? maxlen :
			(first ? strlen(template) * 2 : (*buf)->len * 2));
		pbx_substitute_variables_helper_full(chan, NULL, template, (*buf)->str, (*buf)->len - 1, &((*buf)->used));
		first = 0;
	} while (maxlen == 0 && (*buf)->len - 5 < (*buf)->used);
#endif
}

char *__ast_str_helper2(struct ast_str **buf, size_t maxlen, const char *src, size_t maxsrc, int append, int escapecommas)
{
	int dynamic = 0;
#ifdef DEBUG_OPAQUE
	char *ptr = append ? &((*buf)->str2[(*buf)->used2]) : (*buf)->str2;
#else
	char *ptr = append ? &((*buf)->str[(*buf)->used]) : (*buf)->str;
#endif

	if (!maxlen) {
		dynamic = 1;
#ifdef DEBUG_OPAQUE
		maxlen = (*buf)->len2;
#else
		maxlen = (*buf)->len;
#endif
	}

	while (*src && maxsrc && maxlen && (!escapecommas || (maxlen - 1))) {
		if (escapecommas && (*src == '\\' || *src == ',')) {
			*ptr++ = '\\';
			maxlen--;
#ifdef DEBUG_OPAQUE
			(*buf)->used2++;
#else
			(*buf)->used++;
#endif
		}
		*ptr++ = *src++;
		maxsrc--;
		maxlen--;
#ifdef DEBUG_OPAQUE
		(*buf)->used2++;
#else
		(*buf)->used++;
#endif
		if (dynamic && (!maxlen || (escapecommas && !(maxlen - 1)))) {
#ifdef DEBUG_OPAQUE
			size_t old = (*buf)->len2;
			if (ast_str_make_space(buf, (*buf)->len2 * 2)) {
				/* If the buffer can't be extended, end it. */
				break;
			}
#else
			size_t old = (*buf)->len;
			if (ast_str_make_space(buf, (*buf)->len * 2)) {
				/* If the buffer can't be extended, end it. */
				break;
			}
#endif
			/* What we extended the buffer by */
			maxlen = old;
		}
	}
	if (__builtin_expect(!(maxsrc && maxlen), 0)) {
		ptr--;
	}
	*ptr = '\0';
#ifdef DEBUG_OPAQUE
	(*buf)->used2--;
	return (*buf)->str2;
#else
	(*buf)->used--;
	return (*buf)->str;
#endif
}

