/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
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

/*!
 * \file
 * \brief Format Preference API
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include "asterisk/_private.h"
#include "asterisk/version.h"
#include "asterisk/frame.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"

void ast_codec_pref_convert(struct ast_codec_pref *pref, char *buf, size_t size, int right)
{
	size_t f_len;
	const struct ast_format_list *f_list = ast_get_format_list(&f_len);
	int x, differential = (int) 'A', mem;
	char *from, *to;

	/* TODO re-evaluate this function.  It is using the order of the formats specified
	 * in the global format list in a way that may not be safe. */
	if (right) {
		from = pref->order;
		to = buf;
		mem = size;
	} else {
		to = pref->order;
		from = buf;
		mem = AST_CODEC_PREF_SIZE;
	}

	memset(to, 0, mem);
	for (x = 0; x < AST_CODEC_PREF_SIZE; x++) {
		if (!from[x]) {
			break;
		}
		to[x] = right ? (from[x] + differential) : (from[x] - differential);
		if (!right && to[x] && (to[x] < f_len)) {
			ast_format_set(&pref->formats[x], f_list[to[x]-1].id , 0);
		}
	}
}

int ast_codec_pref_string(struct ast_codec_pref *pref, char *buf, size_t size)
{
	int x;
	struct ast_format format;
	size_t total_len, slen;
	char *formatname;

	memset(buf, 0, size);
	total_len = size;
	buf[0] = '(';
	total_len--;
	for (x = 0; x < AST_CODEC_PREF_SIZE; x++) {
		if (total_len <= 0)
			break;
		if (!(ast_codec_pref_index(pref, x, &format)))
			break;
		if ((formatname = ast_getformatname(&format))) {
			slen = strlen(formatname);
			if (slen > total_len)
				break;
			strncat(buf, formatname, total_len - 1); /* safe */
			total_len -= slen;
		}
		if (total_len && x < AST_CODEC_PREF_SIZE - 1 && ast_codec_pref_index(pref, x + 1, &format)) {
			strncat(buf, "|", total_len - 1); /* safe */
			total_len--;
		}
	}
	if (total_len) {
		strncat(buf, ")", total_len - 1); /* safe */
		total_len--;
	}

	return size - total_len;
}

struct ast_format *ast_codec_pref_index(struct ast_codec_pref *pref, int idx, struct ast_format *result)
{
	if ((idx >= 0) && (idx < sizeof(pref->order)) && pref->formats[idx].id) {
		ast_format_copy(result, &pref->formats[idx]);
	} else {
		ast_format_clear(result);
		return NULL;
	}

	return result;
}

/*! \brief Remove codec from pref list */
void ast_codec_pref_remove(struct ast_codec_pref *pref, struct ast_format *format)
{
	struct ast_codec_pref oldorder;
	int x, y = 0;
	size_t f_len = 0;
	const struct ast_format_list *f_list = ast_get_format_list(&f_len);

	if (!pref->order[0])
		return;

	memcpy(&oldorder, pref, sizeof(oldorder));
	memset(pref, 0, sizeof(*pref));

	for (x = 0; x < f_len; x++) {
		if (!oldorder.order[x])
			break;
		if (f_list[oldorder.order[x]-1].id != format->id) {
			pref->order[y] = oldorder.order[x];
			ast_format_copy(&pref->formats[y], &oldorder.formats[x]);
			pref->framing[y++] = oldorder.framing[x];
		}
	}
}

/*! \brief Append codec to list */
int ast_codec_pref_append(struct ast_codec_pref *pref, struct ast_format *format)
{
	int x, newindex = 0;
	size_t f_len = 0;
	const struct ast_format_list *f_list = ast_get_format_list(&f_len);

	ast_codec_pref_remove(pref, format);

	for (x = 0; x < f_len; x++) {
		if (f_list[x].id == format->id) {
			newindex = x + 1;
			break;
		}
	}

	if (newindex) {
		for (x = 0; x < f_len; x++) {
			if (!pref->order[x]) {
				pref->order[x] = newindex;
				ast_format_copy(&pref->formats[x], format);
				break;
			}
		}
	}

	return x;
}

/*! \brief Prepend codec to list */
void ast_codec_pref_prepend(struct ast_codec_pref *pref, struct ast_format *format, int only_if_existing)
{
	int x, newindex = 0;
	size_t f_len = 0;
	const struct ast_format_list *f_list = ast_get_format_list(&f_len);

	/* First step is to get the codecs "index number" */
	for (x = 0; x < f_len; x++) {
		if (f_list[x].id == format->id) {
			newindex = x + 1;
			break;
		}
	}
	/* Done if its unknown */
	if (!newindex)
		return;

	/* Now find any existing occurrence, or the end */
	for (x = 0; x < AST_CODEC_PREF_SIZE; x++) {
		if (!pref->order[x] || pref->order[x] == newindex)
			break;
	}

	if (only_if_existing && !pref->order[x])
		return;

	/* Move down to make space to insert - either all the way to the end,
	   or as far as the existing location (which will be overwritten) */
	for (; x > 0; x--) {
		pref->order[x] = pref->order[x - 1];
		pref->framing[x] = pref->framing[x - 1];
		ast_format_copy(&pref->formats[x], &pref->formats[x - 1]);
	}

	/* And insert the new entry */
	pref->order[0] = newindex;
	pref->framing[0] = 0; /* ? */
	ast_format_copy(&pref->formats[0], format);
}

/*! \brief Set packet size for codec */
int ast_codec_pref_setsize(struct ast_codec_pref *pref, struct ast_format *format, int framems)
{
	int x, idx = -1;
	size_t f_len = 0;
	const struct ast_format_list *f_list = ast_get_format_list(&f_len);

	for (x = 0; x < f_len; x++) {
		if (f_list[x].id == format->id) {
			idx = x;
			break;
		}
	}

	if (idx < 0)
		return -1;

	/* size validation */
	if (!framems)
		framems = f_list[idx].def_ms;

	if (f_list[idx].inc_ms && framems % f_list[idx].inc_ms) /* avoid division by zero */
		framems -= framems % f_list[idx].inc_ms;

	if (framems < f_list[idx].min_ms)
		framems = f_list[idx].min_ms;

	if (framems > f_list[idx].max_ms)
		framems = f_list[idx].max_ms;

	for (x = 0; x < f_len; x++) {
		if (pref->order[x] == (idx + 1)) {
			pref->framing[x] = framems;
			break;
		}
	}

	return x;
}

/*! \brief Get packet size for codec */
struct ast_format_list ast_codec_pref_getsize(struct ast_codec_pref *pref, struct ast_format *format)
{
	int x, idx = -1, framems = 0;
	struct ast_format_list fmt = { 0, };
	size_t f_len = 0;
	const struct ast_format_list *f_list = ast_get_format_list(&f_len);

	for (x = 0; x < f_len; x++) {
		if (f_list[x].id == format->id) {
			fmt = f_list[x];
			idx = x;
			break;
		}
	}

	for (x = 0; x < f_len; x++) {
		if (pref->order[x] == (idx + 1)) {
			framems = pref->framing[x];
			break;
		}
	}

	/* size validation */
	if (!framems)
		framems = f_list[idx].def_ms;

	if (f_list[idx].inc_ms && framems % f_list[idx].inc_ms) /* avoid division by zero */
		framems -= framems % f_list[idx].inc_ms;

	if (framems < f_list[idx].min_ms)
		framems = f_list[idx].min_ms;

	if (framems > f_list[idx].max_ms)
		framems = f_list[idx].max_ms;

	fmt.cur_ms = framems;

	return fmt;
}

/*! \brief Pick a codec */
struct ast_format *ast_codec_choose(struct ast_codec_pref *pref, struct ast_format_cap *cap, int find_best, struct ast_format *result)
{
	int x, slot, found;
	size_t f_len = 0;
	struct ast_format tmp_fmt;

	const struct ast_format_list *f_list = ast_get_format_list(&f_len);

	ast_format_clear(result);

	for (x = 0; x < f_len; x++) {
		slot = pref->order[x];

		if (!slot)
			break;
		if (ast_format_cap_iscompatible(cap, ast_format_set(&tmp_fmt, f_list[slot-1].id, 0))) {
			found = 1; /*format is found and stored in tmp_fmt */
			break;
		}
	}
	if (found && (AST_FORMAT_GET_TYPE(tmp_fmt.id) == AST_FORMAT_TYPE_AUDIO)) {
		ast_format_copy(result, &tmp_fmt);
		return result;
	}

	ast_debug(4, "Could not find preferred codec - %s\n", find_best ? "Going for the best codec" : "Returning zero codec");

	return find_best ? ast_best_codec(cap, result) : NULL;
}


