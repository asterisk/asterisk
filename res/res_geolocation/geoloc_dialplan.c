/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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

#include "asterisk.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/strings.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/res_geolocation.h"
#include "geoloc_private.h"

static void varlist_to_str(struct ast_variable *list, struct ast_str** buf, size_t len)
{
	struct ast_variable *var = list;

	for (; var; var = var->next) {
		ast_str_append(buf, len, "%s=\"%s\"%s", var->name, var->value, var->next ? "," : "");
	}
}

static int geoloc_profile_read(struct ast_channel *chan,
	const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	char *parsed_data = ast_strdupa(data);
	int index = -1;
	struct ast_datastore *ds;
	struct ast_geoloc_eprofile *eprofile = NULL;
	int profile_count = 0;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(field);
		AST_APP_ARG(index);
	);

	/* Check for zero arguments */
	if (ast_strlen_zero(parsed_data)) {
		ast_log(LOG_ERROR, "%s: Cannot call without arguments\n", cmd);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parsed_data);

	if (ast_strlen_zero(args.field)) {
		ast_log(LOG_ERROR, "%s: Cannot call without a field to query\n", cmd);
		return -1;
	}

	if (!ast_strlen_zero(args.index)) {
		if (sscanf(args.index, "%30d", &index) != 1) {
			ast_log(LOG_ERROR, "%s: profile_index '%s' is invalid\n", cmd, args.index);
			return -1;
		}
	}

	ds = ast_geoloc_datastore_find(chan);
	if (!ds) {
		ast_log(LOG_NOTICE, "%s: There are no geoloc profiles on this channel\n", cmd);
		return -1;
	}

	profile_count = ast_geoloc_datastore_size(ds);

	if (index < 0) {
		if (ast_strings_equal(args.field, "count")) {
			ast_str_append(buf, len, "%d", profile_count);
		} else if (ast_strings_equal(args.field, "inheritable")) {
			ast_str_append(buf, len, "%d", ds->inheritance ? 1 : 0);
		} else {
			ast_log(LOG_ERROR, "%s: Field '%s' is not valid\n", cmd, args.field);
			return -1;
		}

		return 0;
	}

	if (index >= profile_count) {
		ast_log(LOG_ERROR, "%s: index %d is out of range 0 -> %d\n", cmd, index, profile_count);
		return -1;
	}

	eprofile = ast_geoloc_datastore_get_eprofile(ds, index);
	if (!eprofile) {
		ast_log(LOG_ERROR, "%s: Internal Error.  Profile at index %d couldn't be retrieved.\n", cmd, index);
		return -1;
	}

	if (ast_strings_equal(args.field, "id")) {
		ast_str_append(buf, len, "%s", eprofile->id);
	} else if (ast_strings_equal(args.field, "location_reference")) {
		ast_str_append(buf, len, "%s", eprofile->location_reference);
	} else if (ast_strings_equal(args.field, "method")) {
		ast_str_append(buf, len, "%s", eprofile->method);
	} else if (ast_strings_equal(args.field, "geolocation_routing")) {
		ast_str_append(buf, len, "%s", eprofile->geolocation_routing ? "yes" : "no");
	} else if (ast_strings_equal(args.field, "profile_action")) {
		ast_str_append(buf, len, "%s", geoloc_action_to_name(eprofile->action));
	} else if (ast_strings_equal(args.field, "format")) {
		ast_str_append(buf, len, "%s", geoloc_format_to_name(eprofile->format));
	} else if (ast_strings_equal(args.field, "pidf_element")) {
		ast_str_append(buf, len, "%s", geoloc_pidf_element_to_name(eprofile->pidf_element));
	} else if (ast_strings_equal(args.field, "location_source")) {
		ast_str_append(buf, len, "%s", eprofile->location_source);
	} else if (ast_strings_equal(args.field, "location_info")) {
		varlist_to_str(eprofile->location_info, buf, len);
	} else if (ast_strings_equal(args.field, "location_info_refinement")) {
		varlist_to_str(eprofile->location_refinement, buf, len);
	} else if (ast_strings_equal(args.field, "location_variables")) {
		varlist_to_str(eprofile->location_variables, buf, len);
	} else if (ast_strings_equal(args.field, "effective_location")) {
		varlist_to_str(eprofile->effective_location, buf, len);
	} else if (ast_strings_equal(args.field, "usage_rules")) {
		varlist_to_str(eprofile->usage_rules, buf, len);
	} else {
		ast_log(LOG_ERROR, "%s: Field '%s' is not valid\n", cmd, args.field);
		return -1;
	}

	ao2_ref(eprofile, -1);
	return 0;
}

#define TEST_ENUM_VALUE(_cmd, _ep, _field, _value) \
({ \
	enum ast_geoloc_ ## _field v; \
	if (!_ep) { \
		ast_log(LOG_ERROR, "%s: Field %s requires a valid index\n", _cmd, #_field); \
		return -1; \
	} \
	v = geoloc_ ## _field ## _str_to_enum(_value); \
	if (v == AST_GEOLOC_INVALID_VALUE) { \
		ast_log(LOG_ERROR, "%s: %s '%s' is invalid\n", _cmd, #_field, value); \
		return -1; \
	} \
	_ep->_field = v; \
})

#define TEST_VARLIST(_cmd, _ep, _field, _value) \
({ \
	struct ast_variable *_list; \
	if (!_ep) { \
		ast_log(LOG_ERROR, "%s: Field %s requires a valid index\n", _cmd, #_field); \
		return -1; \
	} \
	_list = ast_variable_list_from_quoted_string(_value, ",", "=", "\"" ); \
	if (!_list) { \
		ast_log(LOG_ERROR, "%s: %s '%s' is malformed or contains invalid values", _cmd, #_field, _value); \
		return -1; \
	} \
	ast_variables_destroy(_ep->_field); \
	_ep->_field = _list; \
})

static int geoloc_profile_write(struct ast_channel *chan, const char *cmd, char *data,
	 const char *value)
{
	char *parsed_data = ast_strdupa(data);
	struct ast_datastore *ds;
	RAII_VAR(struct ast_geoloc_eprofile *, eprofile, NULL, ao2_cleanup);
	int profile_count = 0;
	int index = -1;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(field);
		AST_APP_ARG(index);
	);

	/* Check for zero arguments */
	if (ast_strlen_zero(parsed_data)) {
		ast_log(LOG_ERROR, "%s: Cannot call without arguments\n", cmd);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parsed_data);

	if (ast_strlen_zero(args.field)) {
		ast_log(LOG_ERROR, "%s: Cannot call without a field to set\n", cmd);
		return -1;
	}

	if (!ast_strlen_zero(args.index)) {
		if (sscanf(args.index, "%30d", &index) != 1) {
			ast_log(LOG_ERROR, "%s: profile_index '%s' is invalid\n", cmd, args.index);
			return -1;
		}
	}

	ds = ast_geoloc_datastore_find(chan);
	if (!ds) {
		ast_log(LOG_WARNING, "%s: There are no geoloc profiles on this channel\n", cmd);
		return -1;
	}

	profile_count = ast_geoloc_datastore_size(ds);

	if (index >= 0 && index < profile_count) {
		eprofile = ast_geoloc_datastore_get_eprofile(ds, index);
		if (!eprofile) {
			ast_log(LOG_ERROR, "%s: Internal Error.  Profile at index %d couldn't be retrieved.\n", cmd, index);
			return -1;
		}
	} else if (index >= profile_count) {
		ast_log(LOG_ERROR, "%s: index %d is out of range 0 -> %d\n", cmd, index, profile_count);
		return -1;
	} else {
		if (ast_strings_equal(args.field, "inheritable")) {
			ast_geoloc_datastore_set_inheritance(ds, ast_true(value));
 		} else {
			ast_log(LOG_ERROR, "%s: Field '%s' is not valid or requires a profile index\n", cmd, args.field);
			return -1;
		}

		return 0;
	}

	if (ast_strings_equal(args.field, "location_reference")) {
		struct ast_geoloc_location *loc = ast_geoloc_get_location(value);
		ao2_cleanup(loc);
		if (!loc) {
			ast_log(LOG_ERROR, "%s: Location reference '%s' doesn't exist\n", cmd, value);
			return -1;
		}
		ast_string_field_set(eprofile, location_reference, value);
	} else if (ast_strings_equal(args.field, "method")) {
		ast_string_field_set(eprofile, method, value);

	} else if (ast_strings_equal(args.field, "geolocation_routing")) {
		eprofile->geolocation_routing = ast_true(value);

	} else if (ast_strings_equal(args.field, "profile_action")) {
		TEST_ENUM_VALUE(cmd, eprofile, action, value);

	} else if (ast_strings_equal(args.field, "format")) {
		TEST_ENUM_VALUE(cmd, eprofile, format, value);

	} else if (ast_strings_equal(args.field, "pidf_element")) {
		TEST_ENUM_VALUE(cmd, eprofile, pidf_element, value);

	} else if (ast_strings_equal(args.field, "location_info")) {
		TEST_VARLIST(cmd, eprofile, location_info, value);
	} else if (ast_strings_equal(args.field, "location_source")) {
		ast_string_field_set(eprofile, location_source, value);
	} else if (ast_strings_equal(args.field, "location_info_refinement")) {
		TEST_VARLIST(cmd, eprofile, location_refinement, value);
	} else if (ast_strings_equal(args.field, "location_variables")) {
		TEST_VARLIST(cmd, eprofile, location_variables, value);
	} else if (ast_strings_equal(args.field, "effective_location")) {
		TEST_VARLIST(cmd, eprofile, effective_location, value);
	} else if (ast_strings_equal(args.field, "usage_rules")) {
		TEST_VARLIST(cmd, eprofile, usage_rules, value);
	} else {
		ast_log(LOG_ERROR, "%s: Field '%s' is not valid\n", cmd, args.field);
		return -1;
	}

	ast_geoloc_eprofile_refresh_location(eprofile);
	return 0;
}

static struct ast_custom_function geoloc_function = {
	.name = "GEOLOC_PROFILE",
	.read2 = geoloc_profile_read,
	.write = geoloc_profile_write,
};

#define profile_create "GeolocProfileCreate"

static int geoloc_eprofile_create(struct ast_channel *chan, const char *data)
{
	char *parsed_data = ast_strdupa(data);
	struct ast_datastore *ds;
	struct ast_geoloc_eprofile * eprofile;
	int profile_count = 0;
	int index = -1;
	int rc = 0;
	struct ast_str *new_size;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(id);
		AST_APP_ARG(index);
	);

	/* Check for zero arguments */
	if (ast_strlen_zero(parsed_data)) {
		ast_log(LOG_ERROR, "%s: Cannot call without arguments\n", profile_create);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parsed_data);

	if (ast_strlen_zero(args.id)) {
		ast_log(LOG_ERROR, "%s: Cannot call without an id field\n", profile_create);
		return -1;
	}

	if (!ast_strlen_zero(args.index)) {
		if (sscanf(args.index, "%30d", &index) != 1) {
			ast_log(LOG_ERROR, "%s: profile_index '%s' is invalid\n", profile_create, args.index);
			return -1;
		}
	} else {
		index = -1;
	}

	ds = ast_geoloc_datastore_find(chan);
	if (!ds) {
		ast_log(LOG_WARNING, "%s: There are no geoloc profiles on this channel\n", profile_create);
		return -1;
	}

	profile_count = ast_geoloc_datastore_size(ds);
	if (index < -1 || index >= profile_count) {
		ast_log(LOG_ERROR, "%s: Invalid insert_before index '%d'.  It must be 0 to insert at the beginning of the list or -1 to append to the end of the list\n", profile_create, index);
		return -1;
	}

	eprofile = ast_geoloc_eprofile_alloc(args.id);
	if (!eprofile) {
		ast_log(LOG_ERROR, "%s: Could not allocate eprofile '%s'\n", profile_create, args.id);
		return -1;
	}

	ds = ast_geoloc_datastore_find(chan);
	if (!ds) {
		ds = ast_geoloc_datastore_create_from_eprofile(eprofile);
		if (!ds) {
			ao2_ref(eprofile, -1);
			ast_log(LOG_ERROR, "%s: Could not create datastore for eprofile '%s'\n", profile_create, args.id);
			return -1;
		}
		rc = 1;
		ast_channel_datastore_add(chan, ds);
	} else if (index < 0) {
		rc = ast_geoloc_datastore_add_eprofile(ds, eprofile);
		if (rc <= 0) {
			ao2_ref(eprofile, -1);
			ast_log(LOG_ERROR, "%s: Could not add eprofile '%s' to datastore\n", profile_create, args.id);
			return -1;
		}
	} else {
		rc = ast_geoloc_datastore_insert_eprofile(ds, eprofile, index);
		if (rc <= 0) {
			ao2_ref(eprofile, -1);
			ast_log(LOG_ERROR, "%s: Could not insert eprofile '%s' to datastore\n", profile_create, args.id);
			return -1;
		}
	}

	new_size = ast_str_alloca(16);
	ast_str_append(&new_size, 0, "%d", rc);
	pbx_builtin_setvar_helper(chan, "GEOLOC_PROFILE_COUNT", ast_str_buffer(new_size));

	return 0;
}

#define profile_delete "GeolocProfileDelete"

static int geoloc_eprofile_delete(struct ast_channel *chan, const char *data)
{
	char *parsed_data = ast_strdupa(data);
	struct ast_datastore *ds;
	int profile_count = 0;
	int index = -1;
	struct ast_str *new_size;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(index);
	);

	/* Check for zero arguments */
	if (ast_strlen_zero(parsed_data)) {
		ast_log(LOG_ERROR, "%s: Cannot call without arguments\n", profile_delete);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parsed_data);

	if (!ast_strlen_zero(args.index)) {
		if (sscanf(args.index, "%30d", &index) != 1) {
			ast_log(LOG_ERROR, "%s: profile_index '%s' is invalid\n", profile_delete, args.index);
			return -1;
		}
	} else {
		ast_log(LOG_ERROR, "%s: A profile_index is required\n", profile_delete);
		return -1;
	}

	ds = ast_geoloc_datastore_find(chan);
	if (!ds) {
		ast_log(LOG_WARNING, "%s: There are no geoloc profiles on this channel\n", profile_delete);
		return -1;
	}

	profile_count = ast_geoloc_datastore_size(ds);
	if (index < -1 || index >= profile_count) {
		ast_log(LOG_ERROR, "%s: Invalid profile_index '%d'.  It must be between 0 and %d\n",
			profile_create, index, profile_count - 1);
		return -1;
	}

	ast_geoloc_datastore_delete_eprofile(ds, index);
	profile_count = ast_geoloc_datastore_size(ds);

	new_size = ast_str_alloca(16);
	ast_str_append(&new_size, 0, "%d", profile_count);
	pbx_builtin_setvar_helper(chan, "GEOLOC_PROFILE_COUNT", ast_str_buffer(new_size));

	return 0;
}

int geoloc_dialplan_unload(void)
{
	ast_unregister_application(profile_delete);
	ast_unregister_application(profile_create);
	ast_custom_function_unregister(&geoloc_function);

	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_dialplan_load(void)
{
	int res = 0;

	res = ast_custom_function_register(&geoloc_function);
	if (res == 0) {
		res = ast_register_application_xml(profile_create, geoloc_eprofile_create);
	}
	if (res == 0) {
		res = ast_register_application_xml(profile_delete, geoloc_eprofile_delete);
	}

	return res == 0 ? AST_MODULE_LOAD_SUCCESS : AST_MODULE_LOAD_DECLINE;
}

int geoloc_dialplan_reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

