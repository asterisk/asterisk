/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2008, Digium, Inc.
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
 * \brief Asterisk datastore objects
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/_private.h"

#include "asterisk/datastore.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"

struct ast_datastore *__ast_datastore_alloc(
	const struct ast_datastore_info *info, const char *uid, struct ast_module *module,
	const char *file, int line, const char *function)
{
	struct ast_datastore *datastore = NULL;

	/* Make sure we at least have type so we can identify this */
	if (!info) {
		return NULL;
	}

#if defined(__AST_DEBUG_MALLOC)
	if (!(datastore = __ast_calloc(1, sizeof(*datastore), file, line, function))) {
		return NULL;
	}
#else
	if (!(datastore = ast_calloc(1, sizeof(*datastore)))) {
		return NULL;
	}
#endif

	if (!ast_strlen_zero(uid) && !(datastore->uid = ast_strdup(uid))) {
		ast_free(datastore);
		return NULL;
	}

	if (module) {
		datastore->instance = ast_module_get_instance(module);
		if (!datastore->instance) {
			ast_free((void *) datastore->uid);
			ast_free(datastore);
			return NULL;
		}
	}
	datastore->info = info;

	return datastore;
}

int ast_datastore_free(struct ast_datastore *datastore)
{
	int res = 0;

	/* Using the destroy function (if present) destroy the data */
	if (datastore->info->destroy != NULL && datastore->data != NULL) {
		datastore->info->destroy(datastore->data);
		datastore->data = NULL;
	}

	/* Free allocated UID memory */
	if (datastore->uid != NULL) {
		ast_free((void *) datastore->uid);
		datastore->uid = NULL;
	}

	ao2_cleanup(datastore->instance);

	/* Finally free memory used by ourselves */
	ast_free(datastore);

	return res;
}
