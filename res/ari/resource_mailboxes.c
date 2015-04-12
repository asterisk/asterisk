/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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

/*! \file
 *
 * \brief /api-docs/mailboxes.{format} implementation- Mailboxes resources
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

#include "asterisk.h"
#include "asterisk/stasis_app_mailbox.h"

ASTERISK_REGISTER_FILE()

#include "resource_mailboxes.h"

void ast_ari_mailboxes_list(struct ast_variable *headers,
	struct ast_ari_mailboxes_list_args *args,
	struct ast_ari_response *response)
{
	struct ast_json *json;

	if (!(json = stasis_app_mailboxes_to_json())) {
		ast_ari_response_error(response, 500,
			"Internal Server Error", "Error building response");
		return;
	}

	ast_ari_response_ok(response, json);
}
void ast_ari_mailboxes_get(struct ast_variable *headers,
	struct ast_ari_mailboxes_get_args *args,
	struct ast_ari_response *response)
{
	struct ast_json *json;

	switch (stasis_app_mailbox_to_json(args->mailbox_name, &json)) {
	case STASIS_MAILBOX_MISSING:
		ast_ari_response_error(response, 404,
			"Not Found", "Mailbox is does not exist");
		return;
	case STASIS_MAILBOX_ERROR:
		ast_ari_response_error(response, 500,
			"Internal Server Error", "Error building response");
		return;
	case STASIS_MAILBOX_OK:
		ast_ari_response_ok(response, json);
	}
}
void ast_ari_mailboxes_update(struct ast_variable *headers,
	struct ast_ari_mailboxes_update_args *args,
	struct ast_ari_response *response)
{
	if (stasis_app_mailbox_update(args->mailbox_name, args->old_messages, args->new_messages)) {
		ast_ari_response_error(response, 500, "Internal Server Error", "Error updating mailbox");
		return;
	}

	ast_ari_response_no_content(response);
}
void ast_ari_mailboxes_delete(struct ast_variable *headers,
	struct ast_ari_mailboxes_delete_args *args,
	struct ast_ari_response *response)
{
	switch (stasis_app_mailbox_delete(args->mailbox_name)) {
	case STASIS_MAILBOX_MISSING:
		ast_ari_response_error(response, 404,
			"Not Found", "Mailbox does not exist");
		return;
	case STASIS_MAILBOX_ERROR:
		ast_ari_response_error(response, 500,
			"INternal Server Error", "Failed to delete the mailbox");
		return;
	case STASIS_MAILBOX_OK:
		ast_ari_response_no_content(response);
	}
}
