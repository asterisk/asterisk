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
<category name="MENUSELECT_CFLAGS" displayname="Compiler Flags" positive_output="yes">
	<member name="DEBUG_OPAQUE" displayname="Change ast_str internals to detect improper usage" touch_on_change="include/asterisk/strings.h">
		<defaultenabled>yes</defaultenabled>
	</member>
</category>
 ***/

/*** MODULEINFO
	<support_level>core</support_level>
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

#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
int __ast_debug_str_helper(struct ast_str **buf, ssize_t max_len,
	int append, const char *fmt, va_list ap, const char *file, int lineno, const char *function)
#else
int __ast_str_helper(struct ast_str **buf, ssize_t max_len,
	int append, const char *fmt, va_list ap)
#endif
{
	int res, need;
	int offset = (append && (*buf)->__AST_STR_LEN) ? (*buf)->__AST_STR_USED : 0;
	va_list aq;

	do {
		if (max_len < 0) {
			max_len = (*buf)->__AST_STR_LEN;	/* don't exceed the allocated space */
		}
		/*
		 * Ask vsnprintf how much space we need. Remember that vsnprintf
		 * does not count the final <code>'\\0'</code> so we must add 1.
		 */
		va_copy(aq, ap);
		res = vsnprintf((*buf)->__AST_STR_STR + offset, (*buf)->__AST_STR_LEN - offset, fmt, aq);

		need = res + offset + 1;
		/*
		 * If there is not enough space and we are below the max length,
		 * reallocate the buffer and return a message telling to retry.
		 */
		if (need > (*buf)->__AST_STR_LEN && (max_len == 0 || (*buf)->__AST_STR_LEN < max_len) ) {
			int len = (int)(*buf)->__AST_STR_LEN;
			if (max_len && max_len < need) {	/* truncate as needed */
				need = max_len;
			} else if (max_len == 0) {	/* if unbounded, give more room for next time */
				need += 16 + need / 4;
			}
			if (0) {	/* debugging */
				ast_verbose("extend from %d to %d\n", len, need);
			}
			if (
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
					_ast_str_make_space(buf, need, file, lineno, function)
#else
					ast_str_make_space(buf, need)
#endif
				) {
				ast_verbose("failed to extend from %d to %d\n", len, need);
				va_end(aq);
				return AST_DYNSTR_BUILD_FAILED;
			}
			(*buf)->__AST_STR_STR[offset] = '\0';	/* Truncate the partial write. */

			/* Restart va_copy before calling vsnprintf() again. */
			va_end(aq);
			continue;
		}
		va_end(aq);
		break;
	} while (1);
	/* update space used, keep in mind the truncation */
	(*buf)->__AST_STR_USED = (res + offset > (*buf)->__AST_STR_LEN) ? (*buf)->__AST_STR_LEN - 1: res + offset;

	return res;
}

char *__ast_str_helper2(struct ast_str **buf, ssize_t maxlen, const char *src, size_t maxsrc, int append, int escapecommas)
{
	int dynamic = 0;
	char *ptr = append ? &((*buf)->__AST_STR_STR[(*buf)->__AST_STR_USED]) : (*buf)->__AST_STR_STR;

	if (maxlen < 1) {
		if (maxlen == 0) {
			dynamic = 1;
		}
		maxlen = (*buf)->__AST_STR_LEN;
	}

	while (*src && maxsrc && maxlen && (!escapecommas || (maxlen - 1))) {
		if (escapecommas && (*src == '\\' || *src == ',')) {
			*ptr++ = '\\';
			maxlen--;
			(*buf)->__AST_STR_USED++;
		}
		*ptr++ = *src++;
		maxsrc--;
		maxlen--;
		(*buf)->__AST_STR_USED++;

		if ((ptr >= (*buf)->__AST_STR_STR + (*buf)->__AST_STR_LEN - 3) ||
			(dynamic && (!maxlen || (escapecommas && !(maxlen - 1))))) {
			char *oldbase = (*buf)->__AST_STR_STR;
			size_t old = (*buf)->__AST_STR_LEN;
			if (ast_str_make_space(buf, (*buf)->__AST_STR_LEN * 2)) {
				/* If the buffer can't be extended, end it. */
				break;
			}
			/* What we extended the buffer by */
			maxlen = old;

			ptr += (*buf)->__AST_STR_STR - oldbase;
		}
	}
	if (__builtin_expect(!maxlen, 0)) {
		ptr--;
	}
	*ptr = '\0';
	return (*buf)->__AST_STR_STR;
}

static int str_hash(const void *obj, const int flags)
{
	return ast_str_hash(obj);
}

static int str_cmp(void *lhs, void *rhs, int flags)
{
	return strcmp(lhs, rhs) ? 0 : CMP_MATCH;
}

struct ao2_container *ast_str_container_alloc_options(enum ao2_container_opts opts, int buckets)
{
	return ao2_container_alloc_options(opts, buckets, str_hash, str_cmp);
}

int ast_str_container_add(struct ao2_container *str_container, const char *add)
{
	char *ao2_add;

	/* The ao2_add object is immutable so it doesn't need a lock of its own. */
	ao2_add = ao2_alloc_options(strlen(add) + 1, NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!ao2_add) {
		return -1;
	}
	strcpy(ao2_add, add);/* Safe */

	ao2_link(str_container, ao2_add);
	ao2_ref(ao2_add, -1);
	return 0;
}

void ast_str_container_remove(struct ao2_container *str_container, const char *remove)
{
	ao2_find(str_container, remove, OBJ_SEARCH_KEY | OBJ_NODATA | OBJ_UNLINK);
}
