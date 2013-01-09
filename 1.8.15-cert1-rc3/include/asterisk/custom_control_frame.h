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

/*! \file
 * \brief Defines the use of the AST_CONTROL_CUSTOM control frame subclass.
 */

#ifndef _ASTERISK_CUSTOM_FRAME_H
#define _ASTERISK_CUSTOM_FRAME_H

#include "asterisk/config.h"

/*! \brief this is the payload structure used in every AST_CONTROL_CUSTOM frame. */
struct ast_custom_payload;

enum ast_custom_payload_type {
	/*! Custom SIP INFO payload type, used only in the sip channel driver. */
	AST_CUSTOM_SIP_INFO,
};

/*!
 * \brief returns the type of payload the custom payload represents
 *
 * \retval payload type, on success
 * \retval -1, on failure
 */
enum ast_custom_payload_type ast_custom_payload_type(struct ast_custom_payload *type);

/*!
 * \brief returns the length of a custom payload
 *
 * \retval len on success
 * \retval -1 on failure
 */
size_t ast_custom_payload_len(struct ast_custom_payload *type);

/*!
 * \brief Encodes and allocates a sip info custom payload type
 *
 * \retval encoded custom payload on success
 * \retval NULL on failure.
 */
struct ast_custom_payload *ast_custom_payload_sipinfo_encode(struct ast_variable *headers,
	const char *content_type,
	const char *content,
	const char *useragent_filter);

/*!
 * \brief Decodes a sip info custom payload type, returns results in parameters.
 * 
 * \note This is the reverse of the encode function.  Pass in a payload, get the headers
 * content type and content variables back out.  Make sure to free all the variables
 * this function returns.
 *
 * \retval 0, variables allocated and returned in output parameters
 * \retval -1, failure no variables were allocated.
 */
int ast_custom_payload_sipinfo_decode(struct ast_custom_payload *pl,
	struct ast_variable **headers,
	char **content_type,
	char **content,
	char **useragent_filter);

#endif
