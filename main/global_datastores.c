/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2007, Digium, Inc.
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

/*! \file
 *
 * \brief globally-accessible datastore information and callbacks
 *
 * \author Mark Michelson <mmichelson@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/global_datastores.h"

static void secure_call_store_destroy(void *data)
{
	struct ast_secure_call_store *store = data;

	ast_free(store);
}

static void *secure_call_store_duplicate(void *data)
{
	struct ast_secure_call_store *old = data;
	struct ast_secure_call_store *new;

	if (!(new = ast_calloc(1, sizeof(*new)))) {
		return NULL;
	}
	new->signaling = old->signaling;
	new->media = old->media;

	return new;
}
const struct ast_datastore_info secure_call_info = {
	.type = "encrypt-call",
	.destroy = secure_call_store_destroy,
	.duplicate = secure_call_store_duplicate,
};
