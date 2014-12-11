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
 * \brief Sorcery Configuration File Object Wizard
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <regex.h>

#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/astobj2.h"
#include "asterisk/config.h"
#include "asterisk/uuid.h"

/*! \brief Default number of buckets for sorcery objects */
#define DEFAULT_OBJECT_BUCKETS 53

/*! \brief Structure for storing configuration file sourced objects */
struct sorcery_config {
	/*! \brief UUID for identifying us when opening a configuration file */
	char uuid[AST_UUID_STR_LEN];

	/*! \brief Objects retrieved from the configuration file */
	struct ao2_global_obj objects;

	/*! \brief Any specific variable criteria for considering a defined category for this object */
	struct ast_variable *criteria;

	/*! \brief Number of buckets to use for objects */
	unsigned int buckets;

	/*! \brief Enable file level integrity instead of object level */
	unsigned int file_integrity:1;

	/*! \brief Filename of the configuration file */
	char filename[];
};

/*! \brief Structure used for fields comparison */
struct sorcery_config_fields_cmp_params {
	/*! \brief Pointer to the sorcery structure */
	const struct ast_sorcery *sorcery;

	/*! \brief Pointer to the fields to check */
	const struct ast_variable *fields;

	/*! \brief Regular expression for checking object id */
	regex_t *regex;

	/*! \brief Optional container to put object into */
	struct ao2_container *container;
};

static void *sorcery_config_open(const char *data);
static void sorcery_config_load(void *data, const struct ast_sorcery *sorcery, const char *type);
static void sorcery_config_reload(void *data, const struct ast_sorcery *sorcery, const char *type);
static void *sorcery_config_retrieve_id(const struct ast_sorcery *sorcery, void *data, const char *type, const char *id);
static void *sorcery_config_retrieve_fields(const struct ast_sorcery *sorcery, void *data, const char *type, const struct ast_variable *fields);
static void sorcery_config_retrieve_multiple(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects,
					     const struct ast_variable *fields);
static void sorcery_config_retrieve_regex(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const char *regex);
static void sorcery_config_close(void *data);

static struct ast_sorcery_wizard config_object_wizard = {
	.name = "config",
	.open = sorcery_config_open,
	.load = sorcery_config_load,
	.reload = sorcery_config_reload,
	.retrieve_id = sorcery_config_retrieve_id,
	.retrieve_fields = sorcery_config_retrieve_fields,
	.retrieve_multiple = sorcery_config_retrieve_multiple,
	.retrieve_regex = sorcery_config_retrieve_regex,
	.close = sorcery_config_close,
};

/*! \brief Destructor function for sorcery config */
static void sorcery_config_destructor(void *obj)
{
	struct sorcery_config *config = obj;

	ao2_global_obj_release(config->objects);
	ast_rwlock_destroy(&config->objects.lock);
	ast_variables_destroy(config->criteria);
}

/*! \brief Hashing function for sorcery objects */
static int sorcery_config_hash(const void *obj, const int flags)
{
	const char *id = obj;

	return ast_str_hash(flags & OBJ_KEY ? id : ast_sorcery_object_get_id(obj));
}

/*! \brief Comparator function for sorcery objects */
static int sorcery_config_cmp(void *obj, void *arg, int flags)
{
	const char *id = arg;

	return !strcmp(ast_sorcery_object_get_id(obj), flags & OBJ_KEY ? id : ast_sorcery_object_get_id(arg)) ? CMP_MATCH | CMP_STOP : 0;
}

static int sorcery_config_fields_cmp(void *obj, void *arg, int flags)
{
	const struct sorcery_config_fields_cmp_params *params = arg;
	RAII_VAR(struct ast_variable *, objset, NULL, ast_variables_destroy);
	RAII_VAR(struct ast_variable *, diff, NULL, ast_variables_destroy);

	if (params->regex) {
		/* If a regular expression has been provided see if it matches, otherwise move on */
		if (!regexec(params->regex, ast_sorcery_object_get_id(obj), 0, NULL, 0)) {
			ao2_link(params->container, obj);
		}
		return 0;
	} else if (params->fields &&
	    (!(objset = ast_sorcery_objectset_create(params->sorcery, obj)) ||
	     (ast_sorcery_changeset_create(objset, params->fields, &diff)) ||
	     diff)) {
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

static void *sorcery_config_retrieve_fields(const struct ast_sorcery *sorcery, void *data, const char *type, const struct ast_variable *fields)
{
	struct sorcery_config *config = data;
	RAII_VAR(struct ao2_container *, objects, ao2_global_obj_ref(config->objects), ao2_cleanup);
	struct sorcery_config_fields_cmp_params params = {
		.sorcery = sorcery,
		.fields = fields,
		.container = NULL,
	};

	/* If no fields are present return nothing, we require *something*, same goes if no objects exist yet */
	if (!objects || !fields) {
		return NULL;
	}

	return ao2_callback(objects, 0, sorcery_config_fields_cmp, &params);
}

static void *sorcery_config_retrieve_id(const struct ast_sorcery *sorcery, void *data, const char *type, const char *id)
{
	struct sorcery_config *config = data;
	RAII_VAR(struct ao2_container *, objects, ao2_global_obj_ref(config->objects), ao2_cleanup);

	return objects ? ao2_find(objects, id, OBJ_KEY | OBJ_NOLOCK) : NULL;
}

static void sorcery_config_retrieve_multiple(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const struct ast_variable *fields)
{
	struct sorcery_config *config = data;
	RAII_VAR(struct ao2_container *, config_objects, ao2_global_obj_ref(config->objects), ao2_cleanup);
	struct sorcery_config_fields_cmp_params params = {
		.sorcery = sorcery,
		.fields = fields,
		.container = objects,
	};

	if (!config_objects) {
		return;
	}
	ao2_callback(config_objects, 0, sorcery_config_fields_cmp, &params);
}

static void sorcery_config_retrieve_regex(const struct ast_sorcery *sorcery, void *data, const char *type, struct ao2_container *objects, const char *regex)
{
	struct sorcery_config *config = data;
	RAII_VAR(struct ao2_container *, config_objects, ao2_global_obj_ref(config->objects), ao2_cleanup);
	regex_t expression;
	struct sorcery_config_fields_cmp_params params = {
		.sorcery = sorcery,
		.container = objects,
		.regex = &expression,
	};

	if (!config_objects || regcomp(&expression, regex, REG_EXTENDED | REG_NOSUB)) {
		return;
	}

	ao2_callback(config_objects, 0, sorcery_config_fields_cmp, &params);
	regfree(&expression);
}

/*! \brief Internal function which determines if criteria has been met for considering an object set applicable */
static int sorcery_is_criteria_met(struct ast_variable *objset, struct ast_variable *criteria)
{
	RAII_VAR(struct ast_variable *, diff, NULL, ast_variables_destroy);

	return (!criteria || (!ast_sorcery_changeset_create(objset, criteria, &diff) && !diff)) ? 1 : 0;
}

static void sorcery_config_internal_load(void *data, const struct ast_sorcery *sorcery, const char *type, unsigned int reload)
{
	struct sorcery_config *config = data;
	struct ast_flags flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_config *cfg = ast_config_load2(config->filename, config->uuid, flags);
	struct ast_category *category = NULL;
	RAII_VAR(struct ao2_container *, objects, NULL, ao2_cleanup);
	const char *id = NULL;

	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config file '%s'\n", config->filename);
		return;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ast_debug(1, "Config file '%s' was unchanged\n", config->filename);
		return;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Contents of config file '%s' are invalid and cannot be parsed\n", config->filename);
		return;
	}

	if (!(objects = ao2_container_alloc(config->buckets, sorcery_config_hash, sorcery_config_cmp))) {
		ast_log(LOG_ERROR, "Could not create bucket for new objects from '%s', keeping existing objects\n",
			config->filename);
		ast_config_destroy(cfg);
		return;
	}

	while ((category = ast_category_browse_filtered(cfg, NULL, category, NULL))) {
		RAII_VAR(void *, obj, NULL, ao2_cleanup);
		id = ast_category_get_name(category);

		/* If given criteria has not been met skip the category, it is not applicable */
		if (!sorcery_is_criteria_met(ast_category_first(category), config->criteria)) {
			continue;
		}

		if (!(obj = ast_sorcery_alloc(sorcery, type, id)) ||
		    ast_sorcery_objectset_apply(sorcery, obj, ast_category_first(category))) {

			if (config->file_integrity) {
				ast_log(LOG_ERROR, "Config file '%s' could not be loaded due to error with object '%s' of type '%s'\n",
					config->filename, id, type);
				ast_config_destroy(cfg);
				return;
			} else {
				ast_log(LOG_ERROR, "Could not create an object of type '%s' with id '%s' from configuration file '%s'\n",
					type, id, config->filename);
			}

			ao2_cleanup(obj);

			/* To ensure we don't lose the object that already exists we retrieve it from the old objects container and add it to the new one */
			if (!(obj = sorcery_config_retrieve_id(sorcery, data, type, id))) {
				continue;
			}
		}

		ao2_link_flags(objects, obj, OBJ_NOLOCK);
	}

	ao2_global_obj_replace_unref(config->objects, objects);
	ast_config_destroy(cfg);
}

static void sorcery_config_load(void *data, const struct ast_sorcery *sorcery, const char *type)
{
	sorcery_config_internal_load(data, sorcery, type, 0);
}

static void sorcery_config_reload(void *data, const struct ast_sorcery *sorcery, const char *type)
{
	sorcery_config_internal_load(data, sorcery, type, 1);
}

static void *sorcery_config_open(const char *data)
{
	char *tmp = ast_strdupa(data), *filename = strsep(&tmp, ","), *option;
	struct sorcery_config *config;

	if (ast_strlen_zero(filename) || !(config = ao2_alloc_options(sizeof(*config) + strlen(filename) + 1, sorcery_config_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK))) {
		return NULL;
	}

	ast_uuid_generate_str(config->uuid, sizeof(config->uuid));

	ast_rwlock_init(&config->objects.lock);
	config->buckets = DEFAULT_OBJECT_BUCKETS;
	strcpy(config->filename, filename);

	while ((option = strsep(&tmp, ","))) {
		char *name = strsep(&option, "="), *value = option;

		if (!strcasecmp(name, "buckets")) {
			if (sscanf(value, "%30u", &config->buckets) != 1) {
				ast_log(LOG_ERROR, "Unsupported bucket size of '%s' used for configuration file '%s', defaulting to '%d'\n",
					value, filename, DEFAULT_OBJECT_BUCKETS);
			}
		} else if (!strcasecmp(name, "integrity")) {
			if (!strcasecmp(value, "file")) {
				config->file_integrity = 1;
			} else if (!strcasecmp(value, "object")) {
				config->file_integrity = 0;
			} else {
				ast_log(LOG_ERROR, "Unsupported integrity value of '%s' used for configuration file '%s', defaulting to 'object'\n",
					value, filename);
			}
		} else if (!strcasecmp(name, "criteria")) {
			char *field = strsep(&value, "=");
			struct ast_variable *criteria = ast_variable_new(field, value, "");

			if (criteria) {
				criteria->next = config->criteria;
				config->criteria = criteria;
			} else {
				/* This is fatal since not following criteria would potentially yield invalid objects */
				ast_log(LOG_ERROR, "Could not create criteria entry of field '%s' with value '%s' for configuration file '%s'\n",
					field, value, filename);
				ao2_ref(config, -1);
				return NULL;
			}
		} else {
			ast_log(LOG_ERROR, "Unsupported option '%s' used for configuration file '%s'\n", name, filename);
		}
	}

	return config;
}

static void sorcery_config_close(void *data)
{
	struct sorcery_config *config = data;

	ao2_ref(config, -1);
}

static int load_module(void)
{
	if (ast_sorcery_wizard_register(&config_object_wizard)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sorcery_wizard_unregister(&config_object_wizard);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Sorcery Configuration File Object Wizard",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_REALTIME_DRIVER,
);
