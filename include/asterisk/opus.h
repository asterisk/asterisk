/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Lorenzo Miniero <lorenzo@meetecho.com>
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
 * \brief Opus Format Attributes (http://tools.ietf.org/html/draft-ietf-payload-rtp-opus)
 *
 * \author Lorenzo Miniero <lorenzo@meetecho.com>
 */
#ifndef _AST_FORMAT_OPUS_H_
#define _AST_FORMAT_OPUS_H_

/*! Opus format attribute key value pairs, all are accessible through ast_format_get_value()*/
enum opus_attr_keys {
	OPUS_ATTR_KEY_MAX_BITRATE, /*! value is an int (6000-510000 in spec). */
	OPUS_ATTR_KEY_MAX_PLAYRATE, /*! value is an int (8000-48000), maximum output rate the receiver can render. */
	OPUS_ATTR_KEY_MINPTIME, /*! value is an int (3-120 in spec, 10-60 in format.c), decoder's minimum length of time in milliseconds. */
	OPUS_ATTR_KEY_STEREO, /*! value is an int, 1 prefer receiving stereo, 0 prefer mono. */
	OPUS_ATTR_KEY_CBR, /*! value is an int, 1 use constant bitrate, 0 use variable bitrate. */
	OPUS_ATTR_KEY_FEC, /*! value is an int, 1 encode with FEC, 0 do not use FEC. */
	OPUS_ATTR_KEY_DTX, /*! value is an int, 1 dtx is enabled, 0 dtx not enabled. */
	OPUS_ATTR_KEY_SPROP_CAPTURE_RATE, /*! value is an int (8000-48000), likely input rate we're going to produce. */
	OPUS_ATTR_KEY_SPROP_STEREO, /*! value is an int, 1 likely to send stereo, 0 likely to send mono. */
};

#endif /* _AST_FORMAT_OPUS_H */
