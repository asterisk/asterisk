/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

#ifndef _RES_PJSIP_BODY_GENERATOR_TYPES_H
#define _RES_PJSIP_BODY_GENERATOR_TYPES_H

#include "asterisk/pbx.h"

/*!
 * \brief structure used for presence XML bodies
 *
 * This is used for the following body types:
 * \li application/pidf+xml
 * \li application/xpidf+xml
 * \li application/cpim-pidf+xml
 */
struct ast_sip_exten_state_data {
	/*! The extension of the current state change */
	const char *exten;
	/*! The extension state of the change */
	enum ast_extension_states exten_state;
	/*! The presence state of the change */
	enum ast_presence_state presence_state;
	/*! Current device state information */
	struct ao2_container *device_state_info;
	/*! Local dialog URI */
	char local[PJSIP_MAX_URL_SIZE];
	/*! Remote dialog URI */
	char remote[PJSIP_MAX_URL_SIZE];
	/*! Allocation pool */
	pj_pool_t *pool;
};

/*!
 * \brief Message counter used for message-summary XML bodies
 *
 * This is used for application/simple-message-summary bodies.
 */
struct ast_sip_message_accumulator {
	/*! Number of old messages */
	int old_msgs;
	/*! Number of new messages */
	int new_msgs;
};

#endif /* _RES_PJSIP_BODY_GENERATOR_TYPES_H */
