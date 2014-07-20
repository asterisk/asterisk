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
	int x;

	for (x = 0; x < 64; x++) {
		uint64_t tmp = (1ULL << x);

		if ((tmp & bitfield) && ast_format_cap_append(cap, ast_format_compatibility_bitfield2format(tmp), 0)) {
			return -1;
		}
	}

	return 0;
}
