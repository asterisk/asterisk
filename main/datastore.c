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
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/uuid.h"

/*! \brief Number of buckets for datastore container */
#define DATASTORE_BUCKETS 53

struct ast_datastore *__ast_datastore_alloc(const struct ast_datastore_info *info, const char *uid,
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

	datastore->info = info;

	if (!ast_strlen_zero(uid) && !(datastore->uid = ast_strdup(uid))) {
		ast_free(datastore);
		datastore = NULL;
	}

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

	/* Finally free memory used by ourselves */
	ast_free(datastore);

	return res;
}

/*! \brief Hashing function for datastores */
static int datastore_hash(const void *obj, const int flags)
{
	const struct ast_datastore *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->uid;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! \brief Comparator function for datastores */
static int datastore_cmp(void *obj, void *arg, int flags)
{
	const struct ast_datastore *object_left = obj;
	const struct ast_datastore *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->uid;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->uid, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/* Not supported by container. */
		ast_assert(0);
		return 0;
	default:
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	return CMP_MATCH;
}

struct ao2_container *ast_datastores_alloc(void)
{
	return ao2_container_alloc(DATASTORE_BUCKETS, datastore_hash, datastore_cmp);
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
	return ao2_find(datastores, name, OBJ_KEY);
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
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
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
		return NULL;
	}

	ao2_ref(datastore, +1);
	return datastore;
}