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

#include <regex.h>
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

#ifdef __AST_DEBUG_MALLOC
int __ast_debug_str_helper(struct ast_str **buf, ssize_t max_len,
	int append, const char *fmt, va_list ap, const char *file, int lineno, const char *function)
#else
int __ast_str_helper(struct ast_str **buf, ssize_t max_len,
	int append, const char *fmt, va_list ap)
#endif
{
	int res;
	int added;
	int need;
	int offset = (append && (*buf)->__AST_STR_LEN) ? (*buf)->__AST_STR_USED : 0;
	va_list aq;

	if (max_len < 0) {
		max_len = (*buf)->__AST_STR_LEN;	/* don't exceed the allocated space */
	}

	do {
		va_copy(aq, ap);
		res = vsnprintf((*buf)->__AST_STR_STR + offset, (*buf)->__AST_STR_LEN - offset, fmt, aq);
		va_end(aq);

		if (res < 0) {
			/*
			 * vsnprintf write to string failed.
			 * I don't think this is possible with a memory buffer.
			 */
			res = AST_DYNSTR_BUILD_FAILED;
			added = 0;
			break;
		}

		/*
		 * vsnprintf returns how much space we used or would need.
		 * Remember that vsnprintf does not count the nil terminator
		 * so we must add 1.
		 */
		added = res;
		need = offset + added + 1;
		if (need <= (*buf)->__AST_STR_LEN
			|| (max_len && max_len <= (*buf)->__AST_STR_LEN)) {
			/*
			 * There was enough room for the string or we are not
			 * allowed to try growing the string buffer.
			 */
			break;
		}

		/* Reallocate the buffer and try again. */
		if (max_len == 0) {
			/* unbounded, give more room for next time */
			need += 16 + need / 4;
		} else if (max_len < need) {
			/* truncate as needed */
			need = max_len;
		}

		if (
#ifdef __AST_DEBUG_MALLOC
			_ast_str_make_space(buf, need, file, lineno, function)
#else
			ast_str_make_space(buf, need)
#endif
			) {
			ast_log_safe(LOG_VERBOSE, "failed to extend from %d to %d\n",
				(int) (*buf)->__AST_STR_LEN, need);

			res = AST_DYNSTR_BUILD_FAILED;
			break;
		}
	} while (1);

	/* Update space used, keep in mind truncation may be necessary. */
	(*buf)->__AST_STR_USED = ((*buf)->__AST_STR_LEN <= offset + added)
		? (*buf)->__AST_STR_LEN - 1
		: offset + added;

	/* Ensure that the string is terminated. */
	(*buf)->__AST_STR_STR[(*buf)->__AST_STR_USED] = '\0';

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

static int str_sort(const void *lhs, const void *rhs, int flags)
{
	if ((flags & OBJ_SEARCH_MASK) == OBJ_SEARCH_PARTIAL_KEY) {
		return strncmp(lhs, rhs, strlen(rhs));
	} else {
		return strcmp(lhs, rhs);
	}
}

static int str_cmp(void *lhs, void *rhs, int flags)
{
	int cmp = 0;

	if ((flags & OBJ_SEARCH_MASK) == OBJ_SEARCH_PARTIAL_KEY) {
		cmp = strncmp(lhs, rhs, strlen(rhs));
	} else {
		cmp = strcmp(lhs, rhs);
	}

	return cmp ? 0 : CMP_MATCH;
}

//struct ao2_container *ast_str_container_alloc_options(enum ao2_container_opts opts, int buckets)
struct ao2_container *ast_str_container_alloc_options(enum ao2_alloc_opts opts, int buckets)
{
	return ao2_container_alloc_hash(opts, 0, buckets, str_hash, str_sort, str_cmp);
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

char *ast_generate_random_string(char *buf, size_t size)
{
	int i;

	for (i = 0; i < size - 1; ++i) {
		buf[i] = 'a' + (ast_random() % 26);
	}
	buf[i] = '\0';

	return buf;
}

int ast_strings_match(const char *left, const char *op, const char *right)
{
	char *internal_op = (char *)op;
	char *internal_right = (char *)right;
	double left_num;
	double right_num;
	int scan_numeric = 0;

	if (!(left && right)) {
		return 0;
	}

	if (ast_strlen_zero(op)) {
		if (ast_strlen_zero(left) && ast_strlen_zero(right)) {
			return 1;
		}

		if (strlen(right) >= 2 && right[0] == '/' && right[strlen(right) - 1] == '/') {
			internal_op = "regex";
			internal_right = ast_strdupa(right);
			/* strip the leading and trailing '/' */
			internal_right++;
			internal_right[strlen(internal_right) - 1] = '\0';
			goto regex;
		} else {
			internal_op = "=";
			goto equals;
		}
	}

	if (!strcasecmp(op, "like")) {
		char *tok;
		struct ast_str *buffer = ast_str_alloca(128);

		if (!strchr(right, '%')) {
			return !strcmp(left, right);
		} else {
			internal_op = "regex";
			internal_right = ast_strdupa(right);
			tok = strsep(&internal_right, "%");
			ast_str_set(&buffer, 0, "^%s", tok);

			while ((tok = strsep(&internal_right, "%"))) {
				ast_str_append(&buffer, 0, ".*%s", tok);
			}
			ast_str_append(&buffer, 0, "%s", "$");

			internal_right = ast_str_buffer(buffer);
			/* fall through to regex */
		}
	}

regex:
	if (!strcasecmp(internal_op, "regex")) {
		regex_t expression;
		int rc;

		if (regcomp(&expression, internal_right, REG_EXTENDED | REG_NOSUB)) {
			return 0;
		}

		rc = regexec(&expression, left, 0, NULL, 0);
		regfree(&expression);
		return !rc;
	}

equals:
	scan_numeric = (sscanf(left, "%lf", &left_num) && sscanf(internal_right, "%lf", &right_num));

	if (internal_op[0] == '=') {
		if (ast_strlen_zero(left) && ast_strlen_zero(internal_right)) {
			return 1;
		}

		if (scan_numeric) {
			return (left_num == right_num);
		} else {
			return (!strcmp(left, internal_right));
		}
	}

	if (internal_op[0] == '!' && internal_op[1] == '=') {
		if (scan_numeric) {
			return (left_num != right_num);
		} else {
			return !!strcmp(left, internal_right);
		}
	}

	if (internal_op[0] == '<') {
		if (scan_numeric) {
			if (internal_op[1] == '=') {
				return (left_num <= right_num);
			} else {
				return (left_num < right_num);
			}
		} else {
			if (internal_op[1] == '=') {
				return strcmp(left, internal_right) <= 0;
			} else {
				return strcmp(left, internal_right) < 0;
			}
		}
	}

	if (internal_op[0] == '>') {
		if (scan_numeric) {
			if (internal_op[1] == '=') {
				return (left_num >= right_num);
			} else {
				return (left_num > right_num);
			}
		} else {
			if (internal_op[1] == '=') {
				return strcmp(left, internal_right) >= 0;
			} else {
				return strcmp(left, internal_right) > 0;
			}
		}
	}

	return 0;
}


