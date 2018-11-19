/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
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
 *
 * \brief Sorcery In-Memory Object Wizard
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <regex.h>

#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/astobj2.h"

/*! \brief Number of buckets for sorcery objects */
#define OBJECT_BUCKETS 53

static void *sorcery_memory_open(const char *data);
static int sorcery_memory_create(const struct ast_sorcery *sorcery, void *data, void *object);
static void *sorcery_memory_retrieve_id(const struct ast_sorcery *sorcery, void *data, const char *type, const char *id);
static void *sorcery_memory_retrieve_fields(const struct ast_sorcery *sorcery, void *data, const char *type, const struct ast_variable *fields);
static void sorcery_memory_retrieve_multiple(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects,
					     const struct ast_variable *fields);
static void sorcery_memory_retrieve_regex(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const char *regex);
static void sorcery_memory_retrieve_prefix(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const char *prefix, const size_t prefix_len);
static int sorcery_memory_update(const struct ast_sorcery *sorcery, void *data, void *object);
static int sorcery_memory_delete(const struct ast_sorcery *sorcery, void *data, void *object);
static void sorcery_memory_close(void *data);

static struct ast_sorcery_wizard memory_object_wizard = {
	.name = "memory",
	.open = sorcery_memory_open,
	.create = sorcery_memory_create,
	.retrieve_id = sorcery_memory_retrieve_id,
	.retrieve_fields = sorcery_memory_retrieve_fields,
	.retrieve_multiple = sorcery_memory_retrieve_multiple,
	.retrieve_regex = sorcery_memory_retrieve_regex,
	.retrieve_prefix = sorcery_memory_retrieve_prefix,
	.update = sorcery_memory_update,
	.delete = sorcery_memory_delete,
	.close = sorcery_memory_close,
};

/*! \brief Structure used for fields comparison */
struct sorcery_memory_fields_cmp_params {
	/*! \brief Pointer to the sorcery structure */
	const struct ast_sorcery *sorcery;

	/*! \brief Pointer to the fields to check */
	const struct ast_variable *fields;

	/*! \brief Regular expression for checking object id */
	regex_t *regex;

	/*! \brief Prefix for matching object id */
	const char *prefix;

	/*! \brief Prefix length in bytes for matching object id */
	const size_t prefix_len;

	/*! \brief Optional container to put object into */
	struct ao2_container *container;
};

/*! \brief Hashing function for sorcery objects */
static int sorcery_memory_hash(const void *obj, const int flags)
{
	const char *id = obj;

	return ast_str_hash(flags & OBJ_KEY ? id : ast_sorcery_object_get_id(obj));
}

/*! \brief Comparator function for sorcery objects */
static int sorcery_memory_cmp(void *obj, void *arg, int flags)
{
	const char *id = arg;

	return !strcmp(ast_sorcery_object_get_id(obj), flags & OBJ_KEY ? id : ast_sorcery_object_get_id(arg)) ? CMP_MATCH | CMP_STOP : 0;
}

static int sorcery_memory_create(const struct ast_sorcery *sorcery, void *data, void *object)
{
	void *existing;

	ao2_lock(data);

	existing = ao2_find(data, ast_sorcery_object_get_id(object), OBJ_KEY | OBJ_NOLOCK);
	if (existing) {
		ao2_ref(existing, -1);
		ao2_unlock(data);
		return -1;
	}

	ao2_link_flags(data, object, OBJ_NOLOCK);

	ao2_unlock(data);

	return 0;
}

static int sorcery_memory_fields_cmp(void *obj, void *arg, int flags)
{
	const struct sorcery_memory_fields_cmp_params *params = arg;
	RAII_VAR(struct ast_variable *, objset, NULL, ast_variables_destroy);

	if (params->regex) {
		/* If a regular expression has been provided see if it matches, otherwise move on */
		if (!regexec(params->regex, ast_sorcery_object_get_id(obj), 0, NULL, 0)) {
			ao2_link(params->container, obj);
		}
		return 0;
	} else if (params->prefix) {
		if (!strncmp(params->prefix, ast_sorcery_object_get_id(obj), params->prefix_len)) {
			ao2_link(params->container, obj);
		}
		return 0;
	} else if (params->fields &&
	    (!(objset = ast_sorcery_objectset_create(params->sorcery, obj)) ||
	     (!ast_variable_lists_match(objset, params->fields, 0)))) {
		/* If we can't turn the object into an object set OR if differences exist between the fields
		 * passed in and what are present on the object they are not a match.
		 */
		return 0;
	}

	if (params->container) {
		ao2_link(params->container, obj);

		/* As multiple objects are being returned keep going */
		return 0;
	} else {
		/* Immediately stop and return, we only want a single object */
		return CMP_MATCH | CMP_STOP;
	}
}

static void *sorcery_memory_retrieve_fields(const struct ast_sorcery *sorcery, void *data, const char *type, const struct ast_variable *fields)
{
	struct sorcery_memory_fields_cmp_params params = {
		.sorcery = sorcery,
		.fields = fields,
		.container = NULL,
	};

	/* If no fields are present return nothing, we require *something* */
	if (!fields) {
		return NULL;
	}

	return ao2_callback(data, 0, sorcery_memory_fields_cmp, &params);
}

static void *sorcery_memory_retrieve_id(const struct ast_sorcery *sorcery, void *data, const char *type, const char *id)
{
	return ao2_find(data, id, OBJ_KEY);
}

static void sorcery_memory_retrieve_multiple(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const struct ast_variable *fields)
{
	struct sorcery_memory_fields_cmp_params params = {
		.sorcery = sorcery,
		.fields = fields,
		.container = objects,
	};

	ao2_callback(data, 0, sorcery_memory_fields_cmp, &params);
}

static void sorcery_memory_retrieve_regex(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const char *regex)
{
	regex_t expression;
	struct sorcery_memory_fields_cmp_params params = {
		.sorcery = sorcery,
		.container = objects,
		.regex = &expression,
	};

	if (ast_strlen_zero(regex)) {
		regex = ".";
	}

	if (regcomp(&expression, regex, REG_EXTENDED | REG_NOSUB)) {
		return;
	}

	ao2_callback(data, 0, sorcery_memory_fields_cmp, &params);
	regfree(&expression);
}

static void sorcery_memory_retrieve_prefix(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const char *prefix, const size_t prefix_len)
{
	struct sorcery_memory_fields_cmp_params params = {
		.sorcery = sorcery,
		.container = objects,
		.prefix = prefix,
		.prefix_len = prefix_len,
	};

	ao2_callback(data, 0, sorcery_memory_fields_cmp, &params);
}

static int sorcery_memory_update(const struct ast_sorcery *sorcery, void *data, void *object)
{
	RAII_VAR(void *, existing, NULL, ao2_cleanup);

	ao2_lock(data);

	if (!(existing = ao2_find(data, ast_sorcery_object_get_id(object), OBJ_KEY | OBJ_UNLINK))) {
		ao2_unlock(data);
		return -1;
	}

	ao2_link(data, object);

	ao2_unlock(data);

	return 0;
}

static int sorcery_memory_delete(const struct ast_sorcery *sorcery, void *data, void *object)
{
	RAII_VAR(void *, existing, ao2_find(data, ast_sorcery_object_get_id(object), OBJ_KEY | OBJ_UNLINK), ao2_cleanup);

	return existing ? 0 : -1;
}

static void *sorcery_memory_open(const char *data)
{
	return ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, OBJECT_BUCKETS,
		sorcery_memory_hash, NULL, sorcery_memory_cmp);
}

static void sorcery_memory_close(void *data)
{
	ao2_ref(data, -1);
}

static int load_module(void)
{
	if (ast_sorcery_wizard_register(&memory_object_wizard)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sorcery_wizard_unregister(&memory_object_wizard);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Sorcery In-Memory Object Wizard",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_REALTIME_DRIVER,
);
