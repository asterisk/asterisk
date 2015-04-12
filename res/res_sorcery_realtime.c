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
 * \brief Sorcery Realtime Object Wizard
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

/*! \brief They key field used to store the unique identifier for the object */
#define UUID_FIELD "id"

static void *sorcery_realtime_open(const char *data);
static int sorcery_realtime_create(const struct ast_sorcery *sorcery, void *data, void *object);
static void *sorcery_realtime_retrieve_id(const struct ast_sorcery *sorcery, void *data, const char *type, const char *id);
static void *sorcery_realtime_retrieve_fields(const struct ast_sorcery *sorcery, void *data, const char *type, const struct ast_variable *fields);
static void sorcery_realtime_retrieve_multiple(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects,
					     const struct ast_variable *fields);
static void sorcery_realtime_retrieve_regex(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const char *regex);
static int sorcery_realtime_update(const struct ast_sorcery *sorcery, void *data, void *object);
static int sorcery_realtime_delete(const struct ast_sorcery *sorcery, void *data, void *object);
static void sorcery_realtime_close(void *data);

static struct ast_sorcery_wizard realtime_object_wizard = {
	.name = "realtime",
	.open = sorcery_realtime_open,
	.create = sorcery_realtime_create,
	.retrieve_id = sorcery_realtime_retrieve_id,
	.retrieve_fields = sorcery_realtime_retrieve_fields,
	.retrieve_multiple = sorcery_realtime_retrieve_multiple,
	.retrieve_regex = sorcery_realtime_retrieve_regex,
	.update = sorcery_realtime_update,
	.delete = sorcery_realtime_delete,
	.close = sorcery_realtime_close,
};

static int sorcery_realtime_create(const struct ast_sorcery *sorcery, void *data, void *object)
{
	const char *family = data;
	RAII_VAR(struct ast_variable *, fields, ast_sorcery_objectset_create(sorcery, object), ast_variables_destroy);
	struct ast_variable *id = ast_variable_new(UUID_FIELD, ast_sorcery_object_get_id(object), "");

	if (!fields || !id) {
		ast_variables_destroy(id);
		return -1;
	}

	/* Place the identifier at the front for sanity sake */
	id->next = fields;
	fields = id;

	return (ast_store_realtime_fields(family, fields) <= 0) ? -1 : 0;
}

/*! \brief Internal helper function which returns a filtered objectset. 
 *
 * The following are filtered out of the objectset:
 * \li The id field. This is returned to the caller in an out parameter.
 * \li Fields that are not registered with sorcery.
 *
 * \param objectset Objectset to filter.
 * \param[out] id The ID of the sorcery object, as found in the objectset.
 * \param sorcery The sorcery instance that is requesting an objectset.
 * \param type The object type
 *
 * \return The filtered objectset
 */
static struct ast_variable *sorcery_realtime_filter_objectset(struct ast_variable *objectset, struct ast_variable **id,
		const struct ast_sorcery *sorcery, const char *type)
{
	struct ast_variable *previous = NULL, *field = objectset;
	struct ast_sorcery_object_type *object_type;

	object_type = ast_sorcery_get_object_type(sorcery, type);
	if (!object_type) {
		ast_log(LOG_WARNING, "Unknown sorcery object type %s. Expect errors\n", type);
		/* Continue since we still want to filter out the id */
	}

	while (field) {
		int remove_field = 0;
		int delete_field = 0;

		if (!strcmp(field->name, UUID_FIELD)) {
			*id = field;
			remove_field = 1;
		} else if (object_type &&
				!ast_sorcery_is_object_field_registered(object_type, field->name)) {
			ast_debug(1, "Filtering out realtime field '%s' from retrieval\n", field->name);
			remove_field = 1;
			delete_field = 1;
		}

		if (remove_field) {
			struct ast_variable *removed;

			if (previous) {
				previous->next = field->next;
			} else {
				objectset = field->next;
			}

			removed = field;
			field = field->next;
			removed->next = NULL;
			if (delete_field) {
				ast_variables_destroy(removed);
			}
		} else {
			previous = field;
			field = field->next;
		}
	}

	return objectset;
}

static void *sorcery_realtime_retrieve_fields(const struct ast_sorcery *sorcery, void *data, const char *type, const struct ast_variable *fields)
{
	const char *family = data;
	RAII_VAR(struct ast_variable *, objectset, NULL, ast_variables_destroy);
	RAII_VAR(struct ast_variable *, id, NULL, ast_variables_destroy);
	void *object = NULL;

	if (!(objectset = ast_load_realtime_fields(family, fields))) {
		return NULL;
	}

	objectset = sorcery_realtime_filter_objectset(objectset, &id, sorcery, type);

	if (!id
		|| !(object = ast_sorcery_alloc(sorcery, type, id->value))
		|| ast_sorcery_objectset_apply(sorcery, object, objectset)) {
		return NULL;
	}

	return object;
}

static void *sorcery_realtime_retrieve_id(const struct ast_sorcery *sorcery, void *data, const char *type, const char *id)
{
	RAII_VAR(struct ast_variable *, fields, ast_variable_new(UUID_FIELD, id, ""), ast_variables_destroy);

	return sorcery_realtime_retrieve_fields(sorcery, data, type, fields);
}

static void sorcery_realtime_retrieve_multiple(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const struct ast_variable *fields)
{
	const char *family = data;
	RAII_VAR(struct ast_config *, rows, NULL, ast_config_destroy);
	RAII_VAR(struct ast_variable *, all, NULL, ast_variables_destroy);
	struct ast_category *row = NULL;

	if (!fields) {
		char field[strlen(UUID_FIELD) + 6], value[2];

		/* If no fields have been specified we want all rows, so trick realtime into doing it */
		snprintf(field, sizeof(field), "%s LIKE", UUID_FIELD);
		snprintf(value, sizeof(value), "%%");

		if (!(all = ast_variable_new(field, value, ""))) {
			return;
		}

		fields = all;
	}

	if (!(rows = ast_load_realtime_multientry_fields(family, fields))) {
		return;
	}

	while ((row = ast_category_browse_filtered(rows, NULL, row, NULL))) {
		struct ast_variable *objectset = ast_category_detach_variables(row);
		RAII_VAR(struct ast_variable *, id, NULL, ast_variables_destroy);
		RAII_VAR(void *, object, NULL, ao2_cleanup);

		objectset = sorcery_realtime_filter_objectset(objectset, &id, sorcery, type);

		if (id && (object = ast_sorcery_alloc(sorcery, type, id->value)) && !ast_sorcery_objectset_apply(sorcery, object, objectset)) {
			ao2_link(objects, object);
		}

		ast_variables_destroy(objectset);
	}
}

static void sorcery_realtime_retrieve_regex(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const char *regex)
{
	char field[strlen(UUID_FIELD) + 6], value[strlen(regex) + 2];
	RAII_VAR(struct ast_variable *, fields, NULL, ast_variables_destroy);

	/* The realtime API provides no direct ability to do regex so for now we support a limited subset using pattern matching */
	if (regex[0] != '^') {
		return;
	}

	snprintf(field, sizeof(field), "%s LIKE", UUID_FIELD);
	snprintf(value, sizeof(value), "%s%%", regex + 1);

	if (!(fields = ast_variable_new(field, value, ""))) {
		return;
	}

	sorcery_realtime_retrieve_multiple(sorcery, data, type, objects, fields);
}

static int sorcery_realtime_update(const struct ast_sorcery *sorcery, void *data, void *object)
{
	const char *family = data;
	RAII_VAR(struct ast_variable *, fields, ast_sorcery_objectset_create(sorcery, object), ast_variables_destroy);

	if (!fields) {
		return -1;
	}

	return (ast_update_realtime_fields(family, UUID_FIELD, ast_sorcery_object_get_id(object), fields) <= 0) ? -1 : 0;
}

static int sorcery_realtime_delete(const struct ast_sorcery *sorcery, void *data, void *object)
{
	const char *family = data;

	return (ast_destroy_realtime_fields(family, UUID_FIELD, ast_sorcery_object_get_id(object), NULL) <= 0) ? -1 : 0;
}

static void *sorcery_realtime_open(const char *data)
{
	/* We require a prefix for family string generation, or else stuff could mix together */
	if (ast_strlen_zero(data) || !ast_realtime_is_mapping_defined(data)) {
		return NULL;
	}

	return ast_strdup(data);
}

static void sorcery_realtime_close(void *data)
{
	ast_free(data);
}

static int load_module(void)
{
	if (ast_sorcery_wizard_register(&realtime_object_wizard)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sorcery_wizard_unregister(&realtime_object_wizard);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Sorcery Realtime Object Wizard",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_REALTIME_DRIVER,
);
