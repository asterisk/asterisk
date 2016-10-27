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

#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/codec.h"
#include "asterisk/format.h"
#include "asterisk/format_compatibility.h"
#include "asterisk/format_cache.h"
#include "asterisk/format_cap.h"
#include "asterisk/utils.h"

#include "include/codec_pref.h"
#include "include/format_compatibility.h"

void iax2_codec_pref_convert(struct iax2_codec_pref *pref, char *buf, size_t size, int right)
{
	static int differential = (int) 'A';
	int x;

	if (right) {
		--size;/* Save room for the nul string terminator. */
		for (x = 0; x < ARRAY_LEN(pref->order) && x < size; ++x) {
			if (!pref->order[x]) {
				break;
			}

			buf[x] = pref->order[x] + differential;
		}

		buf[x] = '\0';
	} else {
		for (x = 0; x < ARRAY_LEN(pref->order) && x < size; ++x) {
			if (buf[x] == '\0') {
				break;
			}

			pref->order[x] = buf[x] - differential;
			pref->framing[x] = 0;
		}

		if (x < ARRAY_LEN(pref->order)) {
			pref->order[x] = 0;
			pref->framing[x] = 0;
		}
	}
}

struct ast_format *iax2_codec_pref_index(struct iax2_codec_pref *pref, int idx, struct ast_format **result)
{
	if (0 <= idx && idx < ARRAY_LEN(pref->order) && pref->order[idx]) {
		uint64_t pref_bitfield;

		pref_bitfield = iax2_codec_pref_order_value_to_format_bitfield(pref->order[idx]);
		*result = ast_format_compatibility_bitfield2format(pref_bitfield);
	} else {
		*result = NULL;
	}

	return *result;
}

int iax2_codec_pref_to_cap(struct iax2_codec_pref *pref, struct ast_format_cap *cap)
{
	int idx;

	for (idx = 0; idx < ARRAY_LEN(pref->order); ++idx) {
		uint64_t pref_bitfield;
		struct ast_format *pref_format;

		pref_bitfield = iax2_codec_pref_order_value_to_format_bitfield(pref->order[idx]);
		if (!pref_bitfield) {
			break;
		}

		pref_format = ast_format_compatibility_bitfield2format(pref_bitfield);
		if (pref_format && ast_format_cap_append(cap, pref_format, pref->framing[idx])) {
			return -1;
		}
	}
	return 0;
}

int iax2_codec_pref_best_bitfield2cap(uint64_t bitfield, struct iax2_codec_pref *prefs, struct ast_format_cap *cap)
{
	uint64_t best_bitfield;
	struct ast_format *format;

	/* Add any user preferred codecs first. */
	if (prefs) {
		int idx;

		for (idx = 0; bitfield && idx < ARRAY_LEN(prefs->order); ++idx) {
			best_bitfield = iax2_codec_pref_order_value_to_format_bitfield(prefs->order[idx]);
			if (!best_bitfield) {
				break;
			}

			if (best_bitfield & bitfield) {
				format = ast_format_compatibility_bitfield2format(best_bitfield);
				if (format && ast_format_cap_append(cap, format, prefs->framing[idx])) {
					return -1;
				}

				/* Remove just added codec. */
				bitfield &= ~best_bitfield;
			}
		}
	}

	/* Add the hard coded "best" codecs. */
	while (bitfield) {
		best_bitfield = iax2_format_compatibility_best(bitfield);
		if (!best_bitfield) {
			/* No more codecs considered best. */
			break;
		}

		format = ast_format_compatibility_bitfield2format(best_bitfield);
		/* The best_bitfield should always be convertible to a format. */
		ast_assert(format != NULL);

		if (ast_format_cap_append(cap, format, 0)) {
			return -1;
		}

		/* Remove just added "best" codec to find the next "best". */
		bitfield &= ~best_bitfield;
	}

	/* Add any remaining codecs. */
	if (bitfield) {
		int bit;

		for (bit = 0; bit < 64; ++bit) {
			uint64_t mask = (1ULL << bit);

			if (mask & bitfield) {
				format = ast_format_compatibility_bitfield2format(mask);
				if (format && ast_format_cap_append(cap, format, 0)) {
					return -1;
				}
			}
		}
	}

	return 0;
}

int iax2_codec_pref_string(struct iax2_codec_pref *pref, char *buf, size_t size)
{
	int x;
	struct ast_format_cap *cap;
	size_t total_len;
	char *cur;

	/* This function is useless if you have less than a 6 character buffer.
	 * '(...)' is six characters. */
	if (size < 6) {
		return -1;
	}

	/* Convert the preferences into a format cap so that we can read the format names */
	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap || iax2_codec_pref_to_cap(pref, cap)) {
		strcpy(buf, "(...)"); /* Safe */
		ao2_cleanup(cap);
		return -1;
	}

	/* We know that at a minimum, 3 characters are used - (, ), and \0 */
	total_len = size - 3;

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
	int idx;

	idx = codec_pref_index;
	if (idx == ARRAY_LEN(pref->order) - 1) {
		/* Remove from last array entry. */
		pref->order[idx] = 0;
		pref->framing[idx] = 0;
		return;
	}

	for (; idx < ARRAY_LEN(pref->order); ++idx) {
		pref->order[idx] = pref->order[idx + 1];
		pref->framing[idx] = pref->framing[idx + 1];
		if (!pref->order[idx]) {
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

	for (x = 0; x < ARRAY_LEN(pref->order); ++x) {
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
	int idx;

	if (!pref->order[0]) {
		return;
	}

	/*
	 * Work from the end of the list so we always deal with
	 * unmodified entries in case we have to remove a pref.
	 */
	for (idx = ARRAY_LEN(pref->order); idx--;) {
		uint64_t pref_bitfield;

		pref_bitfield = iax2_codec_pref_order_value_to_format_bitfield(pref->order[idx]);
		if (!pref_bitfield) {
			continue;
		}

		/* If this format isn't in the bitfield, remove it from the prefs. */
		if (!(pref_bitfield & bitfield)) {
			codec_pref_remove_index(pref, idx);
		}
	}
}

/*!
 * \brief Formats supported by IAX2.
 *
 * \note All AST_FORMAT_xxx compatibility bit defines must be
 *  represented here.
 *
 * \note The order is important because the array index+1 values
 * go out over the wire.
 */
static const uint64_t iax2_supported_formats[] = {
	AST_FORMAT_G723,
	AST_FORMAT_GSM,
	AST_FORMAT_ULAW,
	AST_FORMAT_ALAW,
	AST_FORMAT_G726,
	AST_FORMAT_ADPCM,
	AST_FORMAT_SLIN,
	AST_FORMAT_LPC10,
	AST_FORMAT_G729,
	AST_FORMAT_SPEEX,
	AST_FORMAT_SPEEX16,
	AST_FORMAT_ILBC,
	AST_FORMAT_G726_AAL2,
	AST_FORMAT_G722,
	AST_FORMAT_SLIN16,
	AST_FORMAT_JPEG,
	AST_FORMAT_PNG,
	AST_FORMAT_H261,
	AST_FORMAT_H263,
	AST_FORMAT_H263P,
	AST_FORMAT_H264,
	AST_FORMAT_MP4,
	AST_FORMAT_T140_RED,
	AST_FORMAT_T140,
	AST_FORMAT_SIREN7,
	AST_FORMAT_SIREN14,
	AST_FORMAT_TESTLAW,
	AST_FORMAT_G719,
	0, /* Place holder */
	0, /* Place holder */
	0, /* Place holder */
	0, /* Place holder */
	0, /* Place holder */
	0, /* Place holder */
	0, /* Place holder */
	0, /* Place holder */
	AST_FORMAT_OPUS,
	AST_FORMAT_VP8,
	/* ONLY ADD TO THE END OF THIS LIST */
	/* XXX Use up the place holder slots first. */
};

uint64_t iax2_codec_pref_order_value_to_format_bitfield(int order_value)
{
	if (order_value < 1 || ARRAY_LEN(iax2_supported_formats) < order_value) {
		return 0;
	}

	return iax2_supported_formats[order_value - 1];
}

int iax2_codec_pref_format_bitfield_to_order_value(uint64_t bitfield)
{
	int idx;

	if (bitfield) {
		for (idx = 0; idx < ARRAY_LEN(iax2_supported_formats); ++idx) {
			if (iax2_supported_formats[idx] == bitfield) {
				return idx + 1;
			}
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief Append the bitfield format to the codec preference list.
 * \since 13.0.0
 *
 * \param pref Codec preference list to append the given bitfield.
 * \param bitfield Format bitfield to append.
 * \param framing Framing size of the codec.
 *
 * \return Nothing
 */
static void iax2_codec_pref_append_bitfield(struct iax2_codec_pref *pref, uint64_t bitfield, unsigned int framing)
{
	int format_index;
	int x;

	format_index = iax2_codec_pref_format_bitfield_to_order_value(bitfield);
	if (!format_index) {
		return;
	}

	codec_pref_remove(pref, format_index);

	for (x = 0; x < ARRAY_LEN(pref->order); ++x) {
		if (!pref->order[x]) {
			pref->order[x] = format_index;
			pref->framing[x] = framing;
			break;
		}
	}
}

void iax2_codec_pref_append(struct iax2_codec_pref *pref, struct ast_format *format, unsigned int framing)
{
	uint64_t bitfield;

	bitfield = ast_format_compatibility_format2bitfield(format);
	if (!bitfield) {
		return;
	}

	iax2_codec_pref_append_bitfield(pref, bitfield, framing);
}

void iax2_codec_pref_prepend(struct iax2_codec_pref *pref, struct ast_format *format, unsigned int framing,
	int only_if_existing)
{
	uint64_t bitfield;
	int format_index;
	int x;

	bitfield = ast_format_compatibility_format2bitfield(format);
	if (!bitfield) {
		return;
	}
	format_index = iax2_codec_pref_format_bitfield_to_order_value(bitfield);
	if (!format_index) {
		return;
	}

	/* Now find any existing occurrence, or the end */
	for (x = 0; x < ARRAY_LEN(pref->order); ++x) {
		if (!pref->order[x] || pref->order[x] == format_index)
			break;
	}

	/*
	 * The array can never be full without format_index
	 * also being in the array.
	 */
	ast_assert(x < ARRAY_LEN(pref->order));

	/* If we failed to find any occurrence, set to the end for safety. */
	if (ARRAY_LEN(pref->order) <= x) {
		x = ARRAY_LEN(pref->order) - 1;
	}

	if (only_if_existing && !pref->order[x]) {
		return;
	}

	/* Move down to make space to insert - either all the way to the end,
	   or as far as the existing location (which will be overwritten) */
	for (; x > 0; --x) {
		pref->order[x] = pref->order[x - 1];
		pref->framing[x] = pref->framing[x - 1];
	}

	/* And insert the new entry */
	pref->order[0] = format_index;
	pref->framing[0] = framing;
}

uint64_t iax2_codec_pref_from_bitfield(struct iax2_codec_pref *pref, uint64_t bitfield)
{
	int bit;
	uint64_t working_bitfield;
	uint64_t best_bitfield;
	struct ast_format *format;

	/* Init the preference list. */
	memset(pref, 0, sizeof(*pref));

	working_bitfield = bitfield;

	/* Add the "best" codecs first. */
	while (working_bitfield) {
		best_bitfield = iax2_format_compatibility_best(working_bitfield);
		if (!best_bitfield) {
			/* No more codecs considered best. */
			break;
		}

		/* Remove current "best" codec to find the next "best". */
		working_bitfield &= ~best_bitfield;

		format = ast_format_compatibility_bitfield2format(best_bitfield);
		/* The best_bitfield should always be convertible to a format. */
		ast_assert(format != NULL);

		iax2_codec_pref_append_bitfield(pref, best_bitfield, 0);
	}

	/* Add any remaining codecs. */
	if (working_bitfield) {
		for (bit = 0; bit < 64; ++bit) {
			uint64_t mask = (1ULL << bit);

			if (mask & working_bitfield) {
				format = ast_format_compatibility_bitfield2format(mask);
				if (!format) {
					/* The bit is not associated with any format. */
					bitfield &= ~mask;
					continue;
				}

				iax2_codec_pref_append_bitfield(pref, mask, 0);
			}
		}
	}

	return bitfield;
}
