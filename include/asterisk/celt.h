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
 * \brief CELT Format Attributes
 *
 * \author David Vossel <dvossel@digium.com>
 */
#ifndef _AST_FORMAT_CELT_H_
#define _AST_FORMAT_CELT_H_

#define AST_CELT_DEFAULT_FRAME_SIZE 480

/*! CELT format attribute key value pairs, all are accessible through ast_format_get_value()*/
enum celt_attr_keys {
	CELT_ATTR_KEY_SAMP_RATE, /*!< value is an unsigned integer representing sample rate */
	CELT_ATTR_KEY_MAX_BITRATE, /*!< value is an int */
	CELT_ATTR_KEY_FRAME_SIZE, /*!< value is an int */
};

#endif /* _AST_FORMAT_CELT_H */
