/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Media Format Bitfield Compatibility API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/codec.h"
#include "asterisk/format.h"
#include "asterisk/format_compatibility.h"
#include "asterisk/format_cache.h"
#include "asterisk/format_cap.h"

#include "include/codec_pref.h"
#include "include/format_compatibility.h"

void iax2_codec_pref_convert(struct iax2_codec_pref *pref, char *buf, size_t size, int right)
{
	static int differential = (int) 'A';
	int x;

	if (right) {
		for (x = 0; x < IAX2_CODEC_PREF_SIZE && x < size; x++) {
			if (!pref->order[x]) {
				break;
			}

			buf[x] = pref->order[x] + differential;
		}

		buf[x] = '\0';
	} else {
		for (x = 0; x < IAX2_CODEC_PREF_SIZE && x < size; x++) {
			if (buf[x] == '\0') {
				break;
			}

			pref->order[x] = buf[x] - differential;
		}

		if (x < size) {
			pref->order[x] = 0;
		}
	}
}

struct ast_format *iax2_codec_pref_index(struct iax2_codec_pref *pref, int idx, struct ast_format **result)
{
	if ((idx >= 0) && (idx < sizeof(pref->order)) && pref->order[idx]) {
		*result = ast_format_compatibility_bitfield2format(pref->order[idx]);
	} else {
		*result = NULL;
	}

	return *result;
}

void iax2_codec_pref_to_cap(struct iax2_codec_pref *pref, struct ast_format_cap *cap)
{
	int idx;

	for (idx = 0; idx < sizeof(pref->order); idx++) {
		if (!pref->order[idx]) {
			break;
		}
		ast_format_cap_append(cap, ast_format_compatibility_bitfield2format(pref->order[idx]), pref->framing[idx]);
	}
}

int iax2_codec_pref_string(struct iax2_codec_pref *pref, char *buf, size_t size)
{
	int x;
	struct ast_format_cap *cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	size_t total_len;
	char *cur;

	if (!cap) {
		return -1;
	}

	/* This function is useless if you have less than a 6 character buffer.
	 * '(...)' is six characters. */
	if (size < 6) {
		return -1;
	}

	/* Convert the preferences into a format cap so that we can read the formst names */
	for (x = 0; x < IAX2_CODEC_PREF_SIZE; x++) {
		uint64_t bitfield = iax2_codec_pref_order_value_to_format_bitfield(pref->order[x]);
		if (!bitfield) {
			break;
		}

		iax2_format_compatibility_bitfield2cap(bitfield, cap);
	}

	/* We know that at a minimum, 3 characters are used - (, ), and \0 */
	total_len = size - 3;

	memset(buf, 0, size);

	/* This character has already been accounted for total_len purposes */
	buf[0] = '(';
	cur = buf + 1;

	/* Loop through the formats and write as many into the buffer as we can */
	for (x = 0; x < ast_format_cap_count(cap); x++) {
		size_t name_len;
		struct ast_format *fmt = ast_format_cap_get_format(cap, x);
		const char *name = ast_format_get_name(fmt);

		name_len = strlen(name);

		/* all entries after the first need a delimiter character */
		if (x) {
			name_len++;
		}

		/* Terminate the list early if we don't have room for the entry.
		 * If it's not the last entry in the list, save enough room to write '...'.
		 */
		if (((x == ast_format_cap_count(cap) - 1) && (total_len < name_len)) ||
				((x < ast_format_cap_count(cap) - 1) && (total_len < name_len + 3))) {
			strcpy(cur, "...");
			cur += 3;
			total_len -= 3;
			ao2_ref(fmt, -1);
			break;
		}

		sprintf(cur, "%s%s", x ? "|" : "", name);
		cur += name_len;
		total_len -= name_len;

		ao2_ref(fmt, -1);
	}
	ao2_ref(cap, -1);

	/* These two characters have already been accounted for total_len purposes */
	cur[0] = ')';
	cur[1] = '\0';

	return size - total_len;
}

static void codec_pref_remove_index(struct iax2_codec_pref *pref, int codec_pref_index)
{
	int x;

	for (x = codec_pref_index; x < IAX2_CODEC_PREF_SIZE; x++) {
		pref->order[x] = pref->order[x + 1];
		pref->framing[x] = pref->framing[x + 1];
		if (!pref->order[x]) {
			return;
		}
	}
}

/*! \brief Remove codec from pref list */
static void codec_pref_remove(struct iax2_codec_pref *pref, int format_index)
{
	int x;

	if (!pref->order[0]) {
		return;
	}

	for (x = 0; x < IAX2_CODEC_PREF_SIZE; x++) {
		if (!pref->order[x]) {
			break;
		}

		if (pref->order[x] == format_index) {
			codec_pref_remove_index(pref, x);
			break;
		}
	}
}

void iax2_codec_pref_remove_missing(struct iax2_codec_pref *pref, uint64_t bitfield)
{
	int x;

	if (!pref->order[0]) {
		return;
	}

	for (x = 0; x < IAX2_CODEC_PREF_SIZE; x++) {
		uint64_t format_as_bitfield = iax2_codec_pref_order_value_to_format_bitfield(pref->order[x]);
		if (!pref->order[x]) {
			break;
		}

		/* If this format isn't in the bitfield, remove it from the prefs. */
		if (!(format_as_bitfield & bitfield)) {
			codec_pref_remove_index(pref, x);
		}
	}
}

uint64_t iax2_codec_pref_order_value_to_format_bitfield(uint64_t order_value)
{
	if (!order_value) {
		return 0;
	}

	return 1 << (order_value - 1);
}

uint64_t iax2_codec_pref_format_bitfield_to_order_value(uint64_t bitfield)
{
	int format_index = 1;

	if (!bitfield) {
		return 0;
	}

	while (bitfield > 1) {
		bitfield = bitfield >> 1;
		format_index++;
	}

	return format_index;
}

/*! \brief Append codec to list */
int iax2_codec_pref_append(struct iax2_codec_pref *pref, struct ast_format *format, unsigned int framing)
{
	uint64_t bitfield = ast_format_compatibility_format2bitfield(format);
	int format_index = iax2_codec_pref_format_bitfield_to_order_value(bitfield);
	int x;

	codec_pref_remove(pref, format_index);

	for (x = 0; x < IAX2_CODEC_PREF_SIZE; x++) {
		if (!pref->order[x]) {
			pref->order[x] = format_index;
			pref->framing[x] = framing;
			break;
		}
	}

	return x;
}

/*! \brief Prepend codec to list */
void iax2_codec_pref_prepend(struct iax2_codec_pref *pref, struct ast_format *format, unsigned int framing,
	int only_if_existing)
{
	uint64_t bitfield = ast_format_compatibility_format2bitfield(format);
	int x;

	/* Now find any existing occurrence, or the end */
	for (x = 0; x < IAX2_CODEC_PREF_SIZE; x++) {
		if (!pref->order[x] || pref->order[x] == bitfield)
			break;
	}

	/* If we failed to find any occurrence, set to the end */
	if (x == IAX2_CODEC_PREF_SIZE) {
		--x;
	}

	if (only_if_existing && !pref->order[x]) {
		return;
	}

	/* Move down to make space to insert - either all the way to the end,
	   or as far as the existing location (which will be overwritten) */
	for (; x > 0; x--) {
		pref->order[x] = pref->order[x - 1];
		pref->framing[x] = pref->framing[x - 1];
	}

	/* And insert the new entry */
	pref->order[0] = bitfield;
	pref->framing[0] = framing;
}

unsigned int iax2_codec_pref_getsize(struct iax2_codec_pref *pref, int idx)
{
	if ((idx >= 0) && (idx < sizeof(pref->order)) && pref->order[idx]) {
		return pref->framing[idx];
	} else {
		return 0;
	}
}

int iax2_codec_pref_setsize(struct iax2_codec_pref *pref, struct ast_format *format, int framems)
{
	int idx;

	for (idx = 0; idx < sizeof(pref->order); idx++) {
		if (!pref->order[idx]) {
			break;
		} else if (ast_format_cmp(ast_format_compatibility_bitfield2format(pref->order[idx]),
			format) != AST_FORMAT_CMP_EQUAL) {
			continue;
		}
		pref->framing[idx] = framems;
		return 0;
	}

	return -1;
}
