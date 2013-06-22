/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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

#ifndef _RES_SIP_EXTEN_STATE_H
#define _RES_SIP_EXTEN_STATE_H

#include "asterisk/stringfields.h"
#include "asterisk/linkedlists.h"

#include "asterisk/pbx.h"
#include "asterisk/presencestate.h"


/*!
 * \brief Contains information pertaining to extension/device state changes.
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
};

/*!
 * \brief Extension state provider.
 */
struct ast_sip_exten_state_provider {
	/*! The name of the event this provider registers for */
	const char *event_name;
	/*! Type of the body, ex: "application" */
	const char *type;
	/*! Subtype of the body, ex: "pidf+xml" */
	const char *subtype;
	/*! Type/Subtype together - ex: application/pidf+xml */
	const char *body_type;
	/*! Subscription handler to be used and associated with provider */
	struct ast_sip_subscription_handler *handler;

	/*!
	 * \brief Create the body text of a NOTIFY request.
	 *
	 * Implementors use this to create body information within the given
	 * ast_str.  That information is then added to the NOTIFY request.
	 *
	 * \param data Current extension state changes
	 * \param local URI of the dialog's local party, e.g. 'from'
	 * \param remote URI of the dialog's remote party, e.g. 'to'
	 * \param body_text Out parameter used to populate the NOTIFY msg body
	 * \retval 0 Successfully created the body's text
	 * \retval -1 Failed to create the body's text
	 */
	int (*create_body)(struct ast_sip_exten_state_data *data, const char *local,
			   const char *remote, struct ast_str **body_text);

	/*! Next item in the list */
	AST_LIST_ENTRY(ast_sip_exten_state_provider) next;
};

/*!
 * \brief Registers an extension state provider.
 *
 * \param obj An extension state provider
 * \retval 0 Successfully registered the extension state provider
 * \retval -1 Failed to register the extension state provider
 */
int ast_sip_register_exten_state_provider(struct ast_sip_exten_state_provider *obj);

/*!
 * \brief Unregisters an extension state provider.
 *
 * \param obj An extension state provider
 */
void ast_sip_unregister_exten_state_provider(struct ast_sip_exten_state_provider *obj);

#endif
