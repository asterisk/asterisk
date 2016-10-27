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

#include "include/format_compatibility.h"

uint64_t iax2_format_compatibility_cap2bitfield(const struct ast_format_cap *cap)
{
	uint64_t bitfield = 0;
	int x;

	for (x = 0; x < ast_format_cap_count(cap); x++) {
		struct ast_format *format = ast_format_cap_get_format(cap, x);

		bitfield |= ast_format_compatibility_format2bitfield(format);

		ao2_ref(format, -1);
	}

	return bitfield;
}

int iax2_format_compatibility_bitfield2cap(uint64_t bitfield, struct ast_format_cap *cap)
{
	int bit;

	for (bit = 0; bit < 64; ++bit) {
		uint64_t mask = (1ULL << bit);

		if (mask & bitfield) {
			struct ast_format *format;

			format = ast_format_compatibility_bitfield2format(mask);
			if (format && ast_format_cap_append(cap, format, 0)) {
				return -1;
			}
		}
	}

	return 0;
}

uint64_t iax2_format_compatibility_best(uint64_t formats)
{
	/*
	 * This just our opinion, expressed in code.  We are
	 * asked to choose the best codec to use, given no
	 * information.
	 */
	static const uint64_t best[] = {
		/*! Okay, ulaw is used by all telephony equipment, so start with it */
		AST_FORMAT_ULAW,
		/*! Unless of course, you're a silly European, so then prefer ALAW */
		AST_FORMAT_ALAW,
		AST_FORMAT_G719,
		AST_FORMAT_SIREN14,
		AST_FORMAT_SIREN7,
		AST_FORMAT_TESTLAW,
		/*! G.722 is better then all below, but not as common as the above... so give ulaw and alaw priority */
		AST_FORMAT_G722,
		/*! Okay, well, signed linear is easy to translate into other stuff */
		AST_FORMAT_SLIN16,
		AST_FORMAT_SLIN,
		/*! G.726 is standard ADPCM, in RFC3551 packing order */
		AST_FORMAT_G726,
		/*! G.726 is standard ADPCM, in AAL2 packing order */
		AST_FORMAT_G726_AAL2,
		/*! ADPCM has great sound quality and is still pretty easy to translate */
		AST_FORMAT_ADPCM,
		/*! Okay, we're down to vocoders now, so pick GSM because it's small and easier to
		    translate and sounds pretty good */
		AST_FORMAT_GSM,
		/*! iLBC is not too bad */
		AST_FORMAT_ILBC,
		/*! Speex is free, but computationally more expensive than GSM */
		AST_FORMAT_SPEEX16,
		AST_FORMAT_SPEEX,
		/*! Opus */
		AST_FORMAT_OPUS,
		/*! Ick, LPC10 sounds terrible, but at least we have code for it, if you're tacky enough
		    to use it */
		AST_FORMAT_LPC10,
		/*! G.729a is faster than 723 and slightly less expensive */
		AST_FORMAT_G729,
		/*! Down to G.723.1 which is proprietary but at least designed for voice */
		AST_FORMAT_G723,
	};
	int idx;

	/* Find the first preferred codec in the format given */
	for (idx = 0; idx < ARRAY_LEN(best); ++idx) {
		if (formats & best[idx]) {
			return best[idx];
		}
	}

	return 0;
}
