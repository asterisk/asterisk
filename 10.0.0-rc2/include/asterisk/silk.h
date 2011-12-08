/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief SILK Format Attributes
 *
 * \author David Vossel <dvossel@digium.com>
 */
#ifndef _AST_FORMAT_SILK_H_
#define _AST_FORMAT_SILK_H_

/*! SILK format attribute key value pairs, all are accessible through ast_format_get_value()*/
enum silk_attr_keys {
	SILK_ATTR_KEY_SAMP_RATE, /*!< value is silk_attr_vals enum */
	SILK_ATTR_KEY_DTX, /*!< value is an int, 1 dtx is enabled, 0 dtx not enabled. */
	SILK_ATTR_KEY_FEC, /*!< value is an int, 1 encode with FEC, 0 do not use FEC. */
	SILK_ATTR_KEY_PACKETLOSS_PERCENTAGE, /*!< value is an int (0-100), Represents estimated packetloss in uplink direction.*/
	SILK_ATTR_KEY_MAX_BITRATE, /*!< value is an int */
};

enum silk_attr_vals {
	SILK_ATTR_VAL_SAMP_8KHZ = (1 << 0),
	SILK_ATTR_VAL_SAMP_12KHZ = (1 << 1),
	SILK_ATTR_VAL_SAMP_16KHZ = (1 << 2),
	SILK_ATTR_VAL_SAMP_24KHZ = (1 << 3),
};

#endif /* _AST_FORMAT_SILK_H */
