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

/*!
 * \file
 * \brief Media Format Bitfield Compatibility API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _IAX2_FORMAT_COMPATIBILITY_H_
#define _IAX2_FORMAT_COMPATIBILITY_H_

struct ast_format;
struct ast_format_cap;

/*!
 * \brief Convert a format capabilities structure to a bitfield
 *
 * \param cap Capabilities structure containing formats
 *
 * \retval non-zero success
 * \retval zero no formats present or no formats supported
 */
uint64_t iax2_format_compatibility_cap2bitfield(const struct ast_format_cap *cap);

/*!
 * \brief Convert a bitfield to a format capabilities structure
 *
 * \param bitfield The bitfield for the media formats
 * \param cap Capabilities structure to place formats into
 *
 * \retval 0 on success.
 * \retval -1 on error.
 *
 * \note If failure occurs the capabilities structure may contain a partial set of formats
 */
int iax2_format_compatibility_bitfield2cap(uint64_t bitfield, struct ast_format_cap *cap);

/*!
 * \brief Pick the best format from the given bitfield formats.
 *
 * \param formats The bitfield for the media formats
 *
 * \retval non-zero Best format out of the given formats.
 * \retval zero No formats present or no formats considered best.
 */
uint64_t iax2_format_compatibility_best(uint64_t formats);

#endif /* _IAX2_FORMAT_COMPATIBILITY_H */
