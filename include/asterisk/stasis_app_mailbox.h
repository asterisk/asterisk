/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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

#ifndef _ASTERISK_STASIS_APP_MAILBOX_H
#define _ASTERISK_STASIS_APP_MAILBOX_H

/*! \file
 *
 * \brief Stasis Application Mailbox API. See \ref res_stasis "Stasis
 * Application API" for detailed documentation.
 *
 * \author Jonathan Rose <kharwell@digium.com>
 * \since 12
 */

#include "asterisk/app.h"
#include "asterisk/stasis_app.h"

/*! Stasis mailbox operation result codes */
enum stasis_mailbox_result {
	/*! Mailbox operation completed successfully */
	STASIS_MAILBOX_OK,
	/*! Mailbox of the requested name does not exist */
	STASIS_MAILBOX_MISSING,
	/*! Mailbox operation failed internally */
	STASIS_MAILBOX_ERROR,
};

/*!
 * \brief Convert mailbox to JSON
 *
 * \param name the name of the mailbox
 * \param json If the query is successful, this pointer at this address will
 *        be set to the JSON representation of the mailbox
 *
 * \return stasis mailbox result code indicating success or failure and cause
 * \retval NULL on error.
 */
enum stasis_mailbox_result stasis_app_mailbox_to_json(const char *name, struct ast_json **json);

/*!
 * \brief Convert mailboxes to json array
 *
 * \return JSON representation of the mailboxes
 * \retval NULL on error.
 */
struct ast_json *stasis_app_mailboxes_to_json(void);

/*!
 * \brief Changes the state of a mailbox.
 *
 * \note Implicitly creates the mailbox.
 *
 * \param name The name of the ARI controlled mailbox
 * \param old_messages count of old (read) messages in the mailbox
 * \param new_messages count of new (unread) messages in the mailbox
 *
 * \retval 0 if successful
 * \retval -1 on internal error.
 */
int stasis_app_mailbox_update(
	const char *name, int old_messages, int new_messages);

/*!
 * \brief Delete a mailbox controlled by ARI.
 *
 * \param name the name of the ARI controlled mailbox
 *
 * \return a stasis mailbox application result
 */
enum stasis_mailbox_result stasis_app_mailbox_delete(
	const char *name);

#endif /* _ASTERISK_STASIS_APP_MAILBOX_H */
