/* -*- C -*-
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Implementation for ARI stubs.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/ast_version.h"
#include "asterisk/buildinfo.h"
#include "asterisk/paths.h"
#include "asterisk/pbx.h"
#include "asterisk/sorcery.h"
#include "resource_asterisk.h"

static void return_sorcery_object(struct ast_sorcery *sorcery, void *sorcery_obj,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_json *, return_set, NULL, ast_json_unref);
	struct ast_variable *change_set;
	struct ast_variable *it_change_set;

	return_set = ast_json_array_create();
	if (!return_set) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	/* Note that we can't use the sorcery JSON change set directly,
	 * as it will hand us back an Object (with fields), and we need
	 * a more generic representation of whatever the API call asked
	 * for, i.e., a list of tuples.
	 */
	change_set = ast_sorcery_objectset_create(sorcery, sorcery_obj);
	if (!change_set) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	for (it_change_set = change_set; it_change_set; it_change_set = it_change_set->next) {
		struct ast_json *tuple;

		tuple = ast_json_pack("{s: s, s: s}",
			"attribute", it_change_set->name,
			"value", it_change_set->value);
		if (!tuple) {
			ast_variables_destroy(change_set);
			ast_ari_response_alloc_failed(response);
			return;
		}

		if (ast_json_array_append(return_set, tuple)) {
			ast_variables_destroy(change_set);
			ast_ari_response_alloc_failed(response);
			return;
		}
	}
	ast_variables_destroy(change_set);

	ast_ari_response_ok(response, ast_json_ref(return_set));
}

void ast_ari_asterisk_get_object(struct ast_variable *headers,
	struct ast_ari_asterisk_get_object_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct ast_sorcery_object_type *, object_type, NULL, ao2_cleanup);
	RAII_VAR(void *, sorcery_obj, NULL, ao2_cleanup);


	sorcery = ast_sorcery_retrieve_by_module_name(args->config_class);
	if (!sorcery) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"configClass '%s' not found",
			args->config_class);
		return;
	}

	object_type = ast_sorcery_get_object_type(sorcery, args->object_type);
	if (!object_type) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"objectType '%s' not found",
			args->object_type);
		return;
	}

	sorcery_obj = ast_sorcery_retrieve_by_id(sorcery, args->object_type, args->id);
	if (!sorcery_obj) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"Object with id '%s' not found",
			args->id);
		return;
	}

	return_sorcery_object(sorcery, sorcery_obj, response);
}

void ast_ari_asterisk_update_object(struct ast_variable *headers, struct ast_ari_asterisk_update_object_args *args, struct ast_ari_response *response)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct ast_sorcery_object_type *, object_type, NULL, ao2_cleanup);
	RAII_VAR(void *, sorcery_obj, NULL, ao2_cleanup);
	struct ast_json *fields;
	struct ast_variable *update_set = NULL;
	int created = 0;

	sorcery = ast_sorcery_retrieve_by_module_name(args->config_class);
	if (!sorcery) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"configClass '%s' not found",
			args->config_class);
		return;
	}

	object_type = ast_sorcery_get_object_type(sorcery, args->object_type);
	if (!object_type) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"objectType '%s' not found",
			args->object_type);
		return;
	}

	sorcery_obj = ast_sorcery_retrieve_by_id(sorcery, args->object_type, args->id);
	if (!sorcery_obj) {
		ast_debug(5, "Sorcery object '%s' does not exist; creating it\n", args->id);
		sorcery_obj = ast_sorcery_alloc(sorcery, args->object_type, args->id);
		if (!sorcery_obj) {
			ast_ari_response_alloc_failed(response);
			return;
		}

		created = 1;
	} else {
		void *copy;

		copy = ast_sorcery_copy(sorcery, sorcery_obj);
		if (!copy) {
			ast_ari_response_alloc_failed(response);
			return;
		}

		ao2_ref(sorcery_obj, -1);
		sorcery_obj = copy;
	}

	fields = ast_json_object_get(args->fields, "fields");
	if (!fields && !created) {
		/* Whoops. We need data. */
		ast_ari_response_error(
			response, 400, "Bad request",
			"Fields must be provided to update object '%s'",
			args->id);
		return;
	} else if (fields) {
		size_t i;

		for (i = 0; i < ast_json_array_size(fields); i++) {
			struct ast_variable *new_var;
			struct ast_json *json_value = ast_json_array_get(fields, i);

			if (!json_value) {
				continue;
			}

			new_var = ast_variable_new(
				ast_json_string_get(ast_json_object_get(json_value, "attribute")),
				ast_json_string_get(ast_json_object_get(json_value, "value")),
				"");
			if (!new_var) {
				ast_variables_destroy(update_set);
				ast_ari_response_alloc_failed(response);
				return;
			}
			ast_variable_list_append(&update_set, new_var);
		}
	}

	/* APPLY! Note that a NULL update_set is fine (and necessary), as it
	 * will force validation on a newly created object.
	 */
	if (ast_sorcery_objectset_apply(sorcery, sorcery_obj, update_set)) {
		ast_variables_destroy(update_set);
		ast_ari_response_error(
			response, 400, "Bad request",
			"%s of object '%s' failed field value validation",
			created ? "Creation" : "Update",
			args->id);
		return;
	}

	ast_variables_destroy(update_set);

	if (created) {
		if (ast_sorcery_create(sorcery, sorcery_obj)) {
			ast_ari_response_error(
				response, 403, "Forbidden",
				"Cannot create sorcery objects of type '%s'",
				args->object_type);
			return;
		}
	} else {
		if (ast_sorcery_update(sorcery, sorcery_obj)) {
			ast_ari_response_error(
				response, 403, "Forbidden",
				"Cannot update sorcery objects of type '%s'",
				args->object_type);
			return;
		}
	}

	return_sorcery_object(sorcery, sorcery_obj, response);
}


void ast_ari_asterisk_delete_object(struct ast_variable *headers,
	struct ast_ari_asterisk_delete_object_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct ast_sorcery_object_type *, object_type, NULL, ao2_cleanup);
	RAII_VAR(void *, sorcery_obj, NULL, ao2_cleanup);

	sorcery = ast_sorcery_retrieve_by_module_name(args->config_class);
	if (!sorcery) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"configClass '%s' not found",
			args->config_class);
		return;
	}

	object_type = ast_sorcery_get_object_type(sorcery, args->object_type);
	if (!object_type) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"objectType '%s' not found",
			args->object_type);
		return;
	}

	sorcery_obj = ast_sorcery_retrieve_by_id(sorcery, args->object_type, args->id);
	if (!sorcery_obj) {
		ast_ari_response_error(
			response, 404, "Not Found",
			"Object with id '%s' not found",
			args->id);
		return;
	}

	if (ast_sorcery_delete(sorcery, sorcery_obj)) {
		ast_ari_response_error(
			response, 403, "Forbidden",
			"Could not delete object with id '%s'",
			args->id);
		return;
	}

	ast_ari_response_no_content(response);
}


void ast_ari_asterisk_get_info(struct ast_variable *headers,
	struct ast_ari_asterisk_get_info_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	int show_all = args->only_count == 0;
	int show_build = show_all;
	int show_system = show_all;
	int show_config = show_all;
	int show_status = show_all;
	size_t i;
	int res = 0;

	for (i = 0; i < args->only_count; ++i) {
		if (strcasecmp("build", args->only[i]) == 0) {
			show_build = 1;
		} else if (strcasecmp("system", args->only[i]) == 0) {
			show_system = 1;
		} else if (strcasecmp("config", args->only[i]) == 0) {
			show_config = 1;
		} else if (strcasecmp("status", args->only[i]) == 0) {
			show_status = 1;
		} else {
			ast_log(LOG_WARNING, "Unrecognized info section '%s'\n",
				args->only[i]);
		}
	}

	json = ast_json_object_create();

	if (show_build) {
		res |= ast_json_object_set(json, "build",
			ast_json_pack(
				"{ s: s, s: s, s: s,"
				"  s: s, s: s, s: s }",

				"os", ast_build_os,
				"kernel", ast_build_kernel,
				"machine", ast_build_machine,

				"options", AST_BUILDOPTS,
				"date", ast_build_date,
				"user", ast_build_user));
	}

	if (show_system) {
		char eid_str[128];

		ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);

		res |= ast_json_object_set(json, "system",
			ast_json_pack("{ s: s, s: s }",
				"version", ast_get_version(),
				"entity_id", eid_str));
	}

	if (show_config) {
		struct ast_json *config = ast_json_pack(
			"{ s: s, s: s,"
			" s: { s: s, s: s } }",

			"name", ast_config_AST_SYSTEM_NAME,
			"default_language", ast_defaultlanguage,

			"setid",
			"user", ast_config_AST_RUN_USER,
			"group", ast_config_AST_RUN_GROUP);

		res |= ast_json_object_set(json, "config", config);

		if (ast_option_maxcalls) {
			res |= ast_json_object_set(config, "max_channels",
				ast_json_integer_create(ast_option_maxcalls));
		}

		if (ast_option_maxfiles) {
			res |= ast_json_object_set(config, "max_open_files",
				ast_json_integer_create(ast_option_maxfiles));
		}

		if (ast_option_maxload) {
			res |= ast_json_object_set(config, "max_load",
				ast_json_real_create(ast_option_maxload));
		}
	}

	if (show_status) {
		res |= ast_json_object_set(json, "status",
			ast_json_pack("{ s: o, s: o }",
				"startup_time",
				ast_json_timeval(ast_startuptime, NULL),
				"last_reload_time",
				ast_json_timeval(ast_lastreloadtime, NULL)));
	}

	if (res != 0) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ast_ari_response_ok(response, ast_json_ref(json));
}

void ast_ari_asterisk_get_global_var(struct ast_variable *headers,
	struct ast_ari_asterisk_get_global_var_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(struct ast_str *, tmp, NULL, ast_free);

	const char *value;

	ast_assert(response != NULL);

	if (ast_strlen_zero(args->variable)) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Variable name is required");
		return;
	}

	tmp = ast_str_create(32);
	if (!tmp) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	value = ast_str_retrieve_variable(&tmp, 0, NULL, NULL, args->variable);

	if (!(json = ast_json_pack("{s: s}", "value", S_OR(value, "")))) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	ast_ari_response_ok(response, ast_json_ref(json));
}

void ast_ari_asterisk_set_global_var(struct ast_variable *headers,
	struct ast_ari_asterisk_set_global_var_args *args,
	struct ast_ari_response *response)
{
	ast_assert(response != NULL);

	if (ast_strlen_zero(args->variable)) {
		ast_ari_response_error(
			response, 400, "Bad Request",
			"Variable name is required");
		return;
	}

	pbx_builtin_setvar_helper(NULL, args->variable, args->value);

	ast_ari_response_no_content(response);
}
