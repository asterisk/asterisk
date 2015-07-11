/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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
 * \brief Sorcery Astdb Object Wizard
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <regex.h>

#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/astdb.h"
#include "asterisk/json.h"

static void *sorcery_astdb_open(const char *data);
static int sorcery_astdb_create(const struct ast_sorcery *sorcery, void *data, void *object);
static void *sorcery_astdb_retrieve_id(const struct ast_sorcery *sorcery, void *data, const char *type, const char *id);
static void *sorcery_astdb_retrieve_fields(const struct ast_sorcery *sorcery, void *data, const char *type, const struct ast_variable *fields);
static void sorcery_astdb_retrieve_multiple(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects,
					     const struct ast_variable *fields);
static void sorcery_astdb_retrieve_regex(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const char *regex);
static int sorcery_astdb_update(const struct ast_sorcery *sorcery, void *data, void *object);
static int sorcery_astdb_delete(const struct ast_sorcery *sorcery, void *data, void *object);
static void sorcery_astdb_close(void *data);

static struct ast_sorcery_wizard astdb_object_wizard = {
	.name = "astdb",
	.open = sorcery_astdb_open,
	.create = sorcery_astdb_create,
	.retrieve_id = sorcery_astdb_retrieve_id,
	.retrieve_fields = sorcery_astdb_retrieve_fields,
	.retrieve_multiple = sorcery_astdb_retrieve_multiple,
	.retrieve_regex = sorcery_astdb_retrieve_regex,
	.update = sorcery_astdb_update,
	.delete = sorcery_astdb_delete,
	.close = sorcery_astdb_close,
};

/*! \brief Helper function which converts from a sorcery object set to a json object */
static struct ast_json *sorcery_objectset_to_json(const struct ast_variable *objectset)
{
	struct ast_json *json = ast_json_object_create();
	const struct ast_variable *field;

	for (field = objectset; field; field = field->next) {
		struct ast_json *value = ast_json_string_create(field->value);

		if (!value) {
			ast_json_unref(json);
			return NULL;
		} else if (ast_json_object_set(json, field->name, value)) {
			ast_json_unref(json);
			return NULL;
		}
	}

	return json;
}

/*! \brief Helper function which converts a json object to a sorcery object set */
static struct ast_variable *sorcery_json_to_objectset(struct ast_json *json)
{
	struct ast_json_iter *field;
	struct ast_variable *objset = NULL;

	for (field = ast_json_object_iter(json); field; field = ast_json_object_iter_next(json, field)) {
		struct ast_json *value = ast_json_object_iter_value(field);
		struct ast_variable *variable = ast_variable_new(ast_json_object_iter_key(field), ast_json_string_get(value), "");

		if (!variable) {
			ast_variables_destroy(objset);
			return NULL;
		}

		variable->next = objset;
		objset = variable;
	}

	return objset;
}

/*! \brief Helper function which compares two json objects and sees if they are equal, but only looks at the criteria provided */
static int sorcery_json_equal(struct ast_json *object, struct ast_json *criteria)
{
	struct ast_json_iter *field;

	for (field = ast_json_object_iter(criteria); field; field = ast_json_object_iter_next(criteria, field)) {
		struct ast_json *object_field = ast_json_object_get(object, ast_json_object_iter_key(field));

		if (!object_field || !ast_json_equal(object_field, ast_json_object_iter_value(field))) {
			return 0;
		}
	}

	return 1;
}

static int sorcery_astdb_create(const struct ast_sorcery *sorcery, void *data, void *object)
{
	RAII_VAR(struct ast_json *, objset, ast_sorcery_objectset_json_create(sorcery, object), ast_json_unref);
	RAII_VAR(char *, value, NULL, ast_json_free);
	const char *prefix = data;
	char family[strlen(prefix) + strlen(ast_sorcery_object_get_type(object)) + 2];

	if (!objset || !(value = ast_json_dump_string(objset))) {
		return -1;
	}

	snprintf(family, sizeof(family), "%s/%s", prefix, ast_sorcery_object_get_type(object));

	return ast_db_put(family, ast_sorcery_object_get_id(object), value);
}

/*! \brief Internal helper function which retrieves an object, or multiple objects, using fields for criteria */
static void *sorcery_astdb_retrieve_fields_common(const struct ast_sorcery *sorcery, void *data, const char *type, const struct ast_variable *fields, struct ao2_container *objects)
{
	const char *prefix = data;
	char family[strlen(prefix) + strlen(type) + 2];
	RAII_VAR(struct ast_db_entry *, entries, NULL, ast_db_freetree);
	RAII_VAR(struct ast_json *, criteria, NULL, ast_json_unref);
	struct ast_db_entry *entry;

	snprintf(family, sizeof(family), "%s/%s", prefix, type);

	if (!(entries = ast_db_gettree(family, NULL)) || (fields && !(criteria = sorcery_objectset_to_json(fields)))) {
		return NULL;
	}

	for (entry = entries; entry; entry = entry->next) {
		const char *key = entry->key + strlen(family) + 2;
		RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
		struct ast_json_error error;
		RAII_VAR(struct ast_variable *, objset, NULL, ast_variables_destroy);
		void *object = NULL;

		if (!(json = ast_json_load_string(entry->data, &error))) {
			return NULL;
		} else if (criteria && !sorcery_json_equal(json, criteria)) {
			continue;
		} else if (!(objset = sorcery_json_to_objectset(json)) ||
			!(object = ast_sorcery_alloc(sorcery, type, key)) ||
			ast_sorcery_objectset_apply(sorcery, object, objset)) {
			ao2_cleanup(object);
			return NULL;
		}

		if (!objects) {
			return object;
		}

		ao2_link(objects, object);
		ao2_cleanup(object);
	}

	return NULL;
}

static void *sorcery_astdb_retrieve_fields(const struct ast_sorcery *sorcery, void *data, const char *type, const struct ast_variable *fields)
{
	return sorcery_astdb_retrieve_fields_common(sorcery, data, type, fields, NULL);
}

static void *sorcery_astdb_retrieve_id(const struct ast_sorcery *sorcery, void *data, const char *type, const char *id)
{
	const char *prefix = data;
	char family[strlen(prefix) + strlen(type) + 2];
	RAII_VAR(char *, value, NULL, ast_free_ptr);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ast_json_error error;
	RAII_VAR(struct ast_variable *, objset, NULL, ast_variables_destroy);
	void *object = NULL;

	snprintf(family, sizeof(family), "%s/%s", prefix, type);

	if (ast_db_get_allocated(family, id, &value) || !(json = ast_json_load_string(value, &error)) ||
		!(objset = sorcery_json_to_objectset(json)) || !(object = ast_sorcery_alloc(sorcery, type, id)) ||
		ast_sorcery_objectset_apply(sorcery, object, objset)) {
		ast_debug(3, "Failed to retrieve object '%s' from astdb\n", id);
		ao2_cleanup(object);
		return NULL;
	}

	return object;
}

static void sorcery_astdb_retrieve_multiple(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const struct ast_variable *fields)
{
	sorcery_astdb_retrieve_fields_common(sorcery, data, type, fields, objects);
}

/*!
 * \internal
 * \brief Convert regex prefix pattern to astDB prefix pattern if possible.
 *
 * \param tree astDB prefix pattern buffer to fill.
 * \param regex Extended regular expression with the start anchor character '^'.
 *
 * \note Since this is a helper function, the tree buffer is
 * assumed to always be large enough.
 *
 * \retval 0 on success.
 * \retval -1 on error.  regex is invalid.
 */
static int make_astdb_prefix_pattern(char *tree, const char *regex)
{
	const char *src;
	char *dst;

	for (dst = tree, src = regex + 1; *src; ++src) {
		if (*src == '\\') {
			/* Escaped regex char. */
			++src;
			if (!*src) {
				/* Invalid regex.  The caller escaped the string terminator. */
				return -1;
			}
		} else if (*src == '$') {
			if (!src[1]) {
				/* Remove the tail anchor character. */
				*dst = '\0';
				return 0;
			}
		} else if (strchr(".?*+{[(|", *src)) {
			/*
			 * The regex is not a simple prefix pattern.
			 *
			 * XXX With more logic, it is possible to simply
			 * use the current prefix pattern.  The last character
			 * needs to be removed if possible when the current regex
			 * token is "?*{".  Also the rest of the regex pattern
			 * would need to be checked for subgroup/alternation.
			 * Subgroup/alternation is too complex for a simple prefix
			 * match.
			 */
			dst = tree;
			break;
		}
		*dst++ = *src;
	}
	if (dst != tree) {
		*dst++ = '%';
	}
	*dst = '\0';
	return 0;
}

static void sorcery_astdb_retrieve_regex(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const char *regex)
{
	const char *prefix = data;
	char family[strlen(prefix) + strlen(type) + 2];
	char tree[strlen(regex) + 1];
	RAII_VAR(struct ast_db_entry *, entries, NULL, ast_db_freetree);
	regex_t expression;
	struct ast_db_entry *entry;

	snprintf(family, sizeof(family), "%s/%s", prefix, type);

	if (regex[0] == '^') {
		/*
		 * For performance reasons, try to create an astDB prefix
		 * pattern from the regex to reduce the number of entries
		 * retrieved from astDB for regex to then match.
		 */
		if (make_astdb_prefix_pattern(tree, regex)) {
			return;
		}
	} else {
		tree[0] = '\0';
	}

	if (!(entries = ast_db_gettree(family, tree))
		|| regcomp(&expression, regex, REG_EXTENDED | REG_NOSUB)) {
		return;
	}

	for (entry = entries; entry; entry = entry->next) {
		/* The key in the entry includes the family, so we need to strip it out for regex purposes */
		const char *key = entry->key + strlen(family) + 2;
		RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
		struct ast_json_error error;
		RAII_VAR(void *, object, NULL, ao2_cleanup);
		RAII_VAR(struct ast_variable *, objset, NULL, ast_variables_destroy);

		if (regexec(&expression, key, 0, NULL, 0)) {
			continue;
		} else if (!(json = ast_json_load_string(entry->data, &error)) ||
			!(objset = sorcery_json_to_objectset(json)) ||
			!(object = ast_sorcery_alloc(sorcery, type, key)) ||
			ast_sorcery_objectset_apply(sorcery, object, objset)) {
			regfree(&expression);
			return;
		}

		ao2_link(objects, object);
	}

	regfree(&expression);
}

static int sorcery_astdb_update(const struct ast_sorcery *sorcery, void *data, void *object)
{
	const char *prefix = data;
	char family[strlen(prefix) + strlen(ast_sorcery_object_get_type(object)) + 2], value[2];

	snprintf(family, sizeof(family), "%s/%s", prefix, ast_sorcery_object_get_type(object));

	/* It is okay for the value to be truncated, we are only checking that it exists */
	if (ast_db_get(family, ast_sorcery_object_get_id(object), value, sizeof(value))) {
		return -1;
	}

	/* The only difference between update and create is that for update the object must already exist */
	return sorcery_astdb_create(sorcery, data, object);
}

static int sorcery_astdb_delete(const struct ast_sorcery *sorcery, void *data, void *object)
{
	const char *prefix = data;
	char family[strlen(prefix) + strlen(ast_sorcery_object_get_type(object)) + 2];
	char value[2];

	snprintf(family, sizeof(family), "%s/%s", prefix, ast_sorcery_object_get_type(object));

	if (ast_db_get(family, ast_sorcery_object_get_id(object), value, sizeof(value))) {
		return -1;
	}

	return ast_db_del(family, ast_sorcery_object_get_id(object));
}

static void *sorcery_astdb_open(const char *data)
{
	/* We require a prefix for family string generation, or else stuff could mix together */
	if (ast_strlen_zero(data)) {
		return NULL;
	}

	return ast_strdup(data);
}

static void sorcery_astdb_close(void *data)
{
	ast_free(data);
}

static int load_module(void)
{
	if (ast_sorcery_wizard_register(&astdb_object_wizard)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sorcery_wizard_unregister(&astdb_object_wizard);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Sorcery Astdb Object Wizard",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_REALTIME_DRIVER,
);
