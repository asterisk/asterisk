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

#include "asterisk/_private.h"

#include "asterisk/datastore.h"
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/uuid.h"
#include "asterisk/module.h"

/*! \brief Number of buckets for datastore container */
#define DATASTORE_BUCKETS 53

struct ast_datastore *__ast_datastore_alloc(
	const struct ast_datastore_info *info, const char *uid, struct ast_module *mod,
	const char *file, int line, const char *function)
{
	struct ast_datastore *datastore = NULL;

	/* Make sure we at least have type so we can identify this */
	if (!info) {
		return NULL;
	}

	datastore = __ast_calloc(1, sizeof(*datastore), file, line, function);
	if (!datastore) {
		return NULL;
	}

	datastore->info = info;
	datastore->mod = mod;

	if (!ast_strlen_zero(uid) && !(datastore->uid = ast_strdup(uid))) {
		ast_free(datastore);
		datastore = NULL;
	}

	ast_module_ref(mod);

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

	ast_module_unref(datastore->mod);

	/* Finally free memory used by ourselves */
	ast_free(datastore);

	return res;
}

AO2_STRING_FIELD_HASH_FN(ast_datastore, uid);
AO2_STRING_FIELD_CMP_FN(ast_datastore, uid);

struct ao2_container *ast_datastores_alloc(void)
{
	return ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		DATASTORE_BUCKETS, ast_datastore_hash_fn, NULL, ast_datastore_cmp_fn);
}

int ast_datastores_add(struct ao2_container *datastores, struct ast_datastore *datastore)
{
	ast_assert(datastore != NULL);
	ast_assert(datastore->info != NULL);
	ast_assert(!ast_strlen_zero(datastore->uid));

	if (!ao2_link(datastores, datastore)) {
		return -1;
	}

	return 0;
}

void ast_datastores_remove(struct ao2_container *datastores, const char *name)
{
	ao2_find(datastores, name, OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NODATA);
}

struct ast_datastore *ast_datastores_find(struct ao2_container *datastores, const char *name)
{
	return ao2_find(datastores, name, OBJ_SEARCH_KEY);
}

static void datastore_destroy(void *obj)
{
	struct ast_datastore *datastore = obj;

	/* Using the destroy function (if present) destroy the data */
	if (datastore->info->destroy != NULL && datastore->data != NULL) {
		datastore->info->destroy(datastore->data);
		datastore->data = NULL;
	}

	ast_free((void *) datastore->uid);
	datastore->uid = NULL;
}

struct ast_datastore *ast_datastores_alloc_datastore(const struct ast_datastore_info *info, const char *uid)
{
	struct ast_datastore *datastore;
	char uuid_buf[AST_UUID_STR_LEN];
	const char *uid_ptr = uid;

	if (!info) {
		return NULL;
	}

	datastore = ao2_alloc(sizeof(*datastore), datastore_destroy);
	if (!datastore) {
		return NULL;
	}

	datastore->info = info;
	if (ast_strlen_zero(uid)) {
		/* They didn't provide an ID so we'll provide one ourself */
		uid_ptr = ast_uuid_generate_str(uuid_buf, sizeof(uuid_buf));
	}

	datastore->uid = ast_strdup(uid_ptr);
	if (!datastore->uid) {
		ao2_ref(datastore, -1);
		return NULL;
	}

	return datastore;
}
