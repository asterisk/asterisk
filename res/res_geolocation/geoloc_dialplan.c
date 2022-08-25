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
#include "geoloc_private.h"

static void varlist_to_str(struct ast_variable *list, struct ast_str** buf, size_t len)
{
	struct ast_variable *var = list;

	for (; var; var = var->next) {
		ast_str_append(buf, len, "%s=\"%s\"%s", var->name, var->value, var->next ? "," : "");
	}
}

#define RESOLVE_FOR_READ(_param) \
({ \
	if (ast_test_flag(&opts, OPT_GEOLOC_RESOLVE)) { \
		struct ast_variable *resolved = geoloc_eprofile_resolve_varlist( \
			eprofile->_param, eprofile->location_variables, chan); \
		if (!resolved) { \
			ast_log(LOG_ERROR, "%s: Unable to resolve " #_param "\n", chan_name); \
			pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-3"); \
			return 0; \
		} \
		varlist_to_str(resolved, buf, len); \
		ast_variables_destroy(resolved); \
	} else { \
		varlist_to_str(eprofile->_param, buf, len); \
	} \
})

enum my_app_option_flags {
	OPT_GEOLOC_RESOLVE = (1 << 0),
	OPT_GEOLOC_APPEND = (1 << 1),
};

AST_APP_OPTIONS(action_options, {
	AST_APP_OPTION('r', OPT_GEOLOC_RESOLVE),
	AST_APP_OPTION('a', OPT_GEOLOC_APPEND),
});


static int geoloc_profile_read(struct ast_channel *chan,
	const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	char *parsed_data = ast_strdupa(data);
	const char *chan_name = ast_channel_name(chan);
	struct ast_datastore *ds;
	struct ast_geoloc_eprofile *orig_eprofile = NULL;
	struct ast_geoloc_eprofile *eprofile = NULL;
	struct ast_flags opts = { 0, };

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(field);
		AST_APP_ARG(options);
	);

	/* Check for zero arguments */
	if (ast_strlen_zero(parsed_data)) {
		ast_log(LOG_ERROR, "%s: Cannot call without arguments\n", chan_name);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-1");
		return 0;
	}

	AST_STANDARD_APP_ARGS(args, parsed_data);

	if (ast_strlen_zero(args.field)) {
		ast_log(LOG_ERROR, "%s: Cannot call without a field to query\n", chan_name);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-1");
		return 0;
	}

	if (!ast_strlen_zero(args.options)) {
		if (ast_app_parse_options(action_options, &opts, NULL, args.options)) {
			ast_log(LOG_ERROR, "%s: Invalid options: %s\n", chan_name, args.options);
			pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-1");
			return 0;
		}
	}

	ds = ast_geoloc_datastore_find(chan);
	if (!ds) {
		ast_log(LOG_NOTICE, "%s: There is no geoloc profile on this channel\n", chan_name);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-2");
		return 0;
	}

	orig_eprofile = ast_geoloc_datastore_get_eprofile(ds, 0);
	if (!orig_eprofile) {
		ast_log(LOG_NOTICE, "%s: There is no geoloc profile on this channel\n", chan_name);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-2");
		return 0;
	}

	eprofile = ast_geoloc_eprofile_dup(orig_eprofile);
	ao2_ref(orig_eprofile, -1);
	if (!eprofile) {
		ast_log(LOG_ERROR, "%s: Unable to duplicate eprofile\n", chan_name);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-2");
		return 0;
	}

	if (!eprofile->effective_location) {
		ast_geoloc_eprofile_refresh_location(eprofile);
	}

	pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "0");
	if (ast_strings_equal(args.field, "inheritable")) {
		ast_str_append(buf, len, "%s", ds->inheritance ? "true" : "false");
	} else if (ast_strings_equal(args.field, "id")) {
		ast_str_append(buf, len, "%s", eprofile->id);
	} else if (ast_strings_equal(args.field, "location_reference")) {
		ast_str_append(buf, len, "%s", eprofile->location_reference);
	} else if (ast_strings_equal(args.field, "method")) {
		ast_str_append(buf, len, "%s", eprofile->method);
	} else if (ast_strings_equal(args.field, "allow_routing_use")) {
		ast_str_append(buf, len, "%s", eprofile->allow_routing_use ? "yes" : "no");
	} else if (ast_strings_equal(args.field, "suppress_empty_ca_elements")) {
		ast_str_append(buf, len, "%s", eprofile->suppress_empty_ca_elements ? "yes" : "no");
	} else if (ast_strings_equal(args.field, "profile_precedence")) {
		ast_str_append(buf, len, "%s", ast_geoloc_precedence_to_name(eprofile->precedence));
	} else if (ast_strings_equal(args.field, "format")) {
		ast_str_append(buf, len, "%s", ast_geoloc_format_to_name(eprofile->format));
	} else if (ast_strings_equal(args.field, "pidf_element")) {
		ast_str_append(buf, len, "%s", ast_geoloc_pidf_element_to_name(eprofile->pidf_element));
	} else if (ast_strings_equal(args.field, "location_source")) {
		ast_str_append(buf, len, "%s", eprofile->location_source);
	} else if (ast_strings_equal(args.field, "notes")) {
		ast_str_append(buf, len, "%s", eprofile->notes);
	} else if (ast_strings_equal(args.field, "location_info")) {
		RESOLVE_FOR_READ(location_info);
	} else if (ast_strings_equal(args.field, "location_info_refinement")) {
		RESOLVE_FOR_READ(location_refinement);
	} else if (ast_strings_equal(args.field, "location_variables")) {
		RESOLVE_FOR_READ(location_variables);
	} else if (ast_strings_equal(args.field, "effective_location")) {
		RESOLVE_FOR_READ(effective_location);
	} else if (ast_strings_equal(args.field, "usage_rules")) {
		RESOLVE_FOR_READ(usage_rules);
	} else if (ast_strings_equal(args.field, "confidence")) {
		varlist_to_str(eprofile->confidence, buf, len);
	} else {
		ast_log(LOG_ERROR, "%s: Field '%s' is not valid\n", chan_name, args.field);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-3");
	}

	ao2_ref(eprofile, -1);
	return 0;
}

#define VAR_LIST_REPLACE(_old, _new) \
	ast_variables_destroy(_old); \
	_old = _new;

#define TEST_ENUM_VALUE(_chan_name, _ep, _field, _value) \
({ \
	enum ast_geoloc_ ## _field v; \
	v = ast_geoloc_ ## _field ## _str_to_enum(_value); \
	if (v == AST_GEOLOC_INVALID_VALUE) { \
		ast_log(LOG_ERROR, "%s: %s '%s' is invalid\n", _chan_name, #_field, value); \
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-3"); \
		return 0; \
	} \
	_ep->_field = v; \
})

#define TEST_VARLIST(_chan_name, _ep, _field, _value) \
({ \
	struct ast_variable *_list; \
	_list = ast_variable_list_from_quoted_string(_value, ",", "=", "\"" ); \
	if (!_list) { \
		ast_log(LOG_ERROR, "%s: %s '%s' is malformed or contains invalid values", _chan_name, #_field, _value); \
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-3"); \
		return 0; \
	} \
	if (ast_test_flag(&opts, OPT_GEOLOC_APPEND)) { \
		ast_variable_list_append(&_ep->_field, _list); \
	} else {\
		VAR_LIST_REPLACE(_ep->_field, _list); \
	} \
})


#define RESOLVE_FOR_WRITE(_param) \
({ \
if (ast_test_flag(&opts, OPT_GEOLOC_RESOLVE)) { \
	struct ast_variable *resolved = geoloc_eprofile_resolve_varlist( \
		eprofile->_param, eprofile->location_variables, chan); \
	if (!resolved) { \
		ast_log(LOG_ERROR, "%s: Unable to resolve " #_param " %p %p\n", chan_name, eprofile->_param, eprofile->location_variables); \
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-3"); \
		return 0; \
	} \
	VAR_LIST_REPLACE(eprofile->_param, resolved); \
} \
})

static int geoloc_profile_write(struct ast_channel *chan, const char *cmd, char *data,
	 const char *value)
{
	char *parsed_data = ast_strdupa(data);
	const char *chan_name = ast_channel_name(chan);
	struct ast_datastore *ds; /* Reminder: datastores aren't ao2 objects */
	RAII_VAR(struct ast_geoloc_eprofile *, eprofile, NULL, ao2_cleanup);
	struct ast_flags opts = { 0, };

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(field);
		AST_APP_ARG(options);
	);

	/* Check for zero arguments */
	if (ast_strlen_zero(parsed_data)) {
		ast_log(LOG_ERROR, "%s: Cannot call without arguments\n", chan_name);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-1");
		return 0;
	}

	AST_STANDARD_APP_ARGS(args, parsed_data);

	if (ast_strlen_zero(args.field)) {
		ast_log(LOG_ERROR, "%s: Cannot call without a field to set\n", chan_name);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-1");
		return 0;
	}

	if (!ast_strlen_zero(args.options)) {
		if (ast_app_parse_options(action_options, &opts, NULL, args.options)) {
			ast_log(LOG_ERROR, "%s: Invalid options: %s\n", chan_name, args.options);
			pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-1");
			return 0;
		}
	}

	ast_debug(1, "%s: name: %s value: %s  options: %s append: %s resolve: %s\n", chan_name,
		args.field, value, args.options, ast_test_flag(&opts, OPT_GEOLOC_APPEND) ? "yes" : "no",
			ast_test_flag(&opts, OPT_GEOLOC_RESOLVE) ? "yes" : "no");

	ds = ast_geoloc_datastore_find(chan);
	if (!ds) {
		ds = ast_geoloc_datastore_create(ast_channel_name(chan));
		if (!ds) {
			ast_log(LOG_WARNING, "%s: Unable to create geolocation datastore\n", chan_name);
			pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-2");
			return 0;
		}
		ast_channel_datastore_add(chan, ds);
	}

	eprofile = ast_geoloc_datastore_get_eprofile(ds, 0);
	if (!eprofile) {
		int rc;
		eprofile = ast_geoloc_eprofile_alloc(chan_name);
		if (!eprofile) {
			ast_log(LOG_ERROR, "%s: Could not allocate eprofile\n", chan_name);
			pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-2");
			return 0;
		}
		rc = ast_geoloc_datastore_add_eprofile(ds, eprofile);
		if (rc <= 0) {
			ast_log(LOG_ERROR, "%s: Could not add eprofile to datastore\n", chan_name);
			pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-2");
			return 0;
		}
	}

	if (ast_strings_equal(args.field, "inheritable")) {
		ast_geoloc_datastore_set_inheritance(ds, ast_true(value));
	} else if (ast_strings_equal(args.field, "id")) {
		ast_string_field_set(eprofile, id, value);
	} else if (ast_strings_equal(args.field, "location_reference")) {
		struct ast_geoloc_location *loc = ast_geoloc_get_location(value);
		ao2_cleanup(loc);
		if (!loc) {
			ast_log(LOG_ERROR, "%s: Location reference '%s' doesn't exist\n", chan_name, value);
			pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-3");
			return 0;
		}
		ast_string_field_set(eprofile, location_reference, value);
	} else if (ast_strings_equal(args.field, "method")) {
		ast_string_field_set(eprofile, method, value);
	} else if (ast_strings_equal(args.field, "allow_routing_use")) {
		eprofile->allow_routing_use = ast_true(value);
	} else if (ast_strings_equal(args.field, "suppress_empty_ca_elements")) {
		eprofile->suppress_empty_ca_elements = ast_true(value);
	} else if (ast_strings_equal(args.field, "profile_precedence")) {
		TEST_ENUM_VALUE(chan_name, eprofile, precedence, value);
	} else if (ast_strings_equal(args.field, "format")) {
		TEST_ENUM_VALUE(chan_name, eprofile, format, value);
	} else if (ast_strings_equal(args.field, "pidf_element")) {
		TEST_ENUM_VALUE(chan_name, eprofile, pidf_element, value);
	} else if (ast_strings_equal(args.field, "location_source")) {
		ast_string_field_set(eprofile, location_source, value);
	} else if (ast_strings_equal(args.field, "notes")) {
		ast_string_field_set(eprofile, notes, value);
	} else if (ast_strings_equal(args.field, "location_info")) {
		TEST_VARLIST(chan_name, eprofile, location_info, value);
		RESOLVE_FOR_WRITE(location_info);
	} else if (ast_strings_equal(args.field, "location_info_refinement")) {
		TEST_VARLIST(chan_name, eprofile, location_refinement, value);
		RESOLVE_FOR_WRITE(location_refinement);
	} else if (ast_strings_equal(args.field, "location_variables")) {
		TEST_VARLIST(chan_name, eprofile, location_variables, value);
		RESOLVE_FOR_WRITE(location_variables);
	} else if (ast_strings_equal(args.field, "effective_location")) {
		TEST_VARLIST(chan_name, eprofile, effective_location, value);
		RESOLVE_FOR_WRITE(effective_location);
	} else if (ast_strings_equal(args.field, "usage_rules")) {
		TEST_VARLIST(chan_name, eprofile, usage_rules, value);
		RESOLVE_FOR_WRITE(usage_rules);
	} else if (ast_strings_equal(args.field, "confidence")) {
		TEST_VARLIST(chan_name, eprofile, confidence, value);
	} else {
		ast_log(LOG_ERROR, "%s: Field '%s' is not valid\n", chan_name, args.field);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-3");
		return 0;
	}

	ast_geoloc_eprofile_refresh_location(eprofile);

	pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "0");

	return 0;
}

static struct ast_custom_function geoloc_function = {
	.name = "GEOLOC_PROFILE",
	.read2 = geoloc_profile_read,
	.write = geoloc_profile_write,
};

int geoloc_dialplan_unload(void)
{
	ast_custom_function_unregister(&geoloc_function);

	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_dialplan_load(void)
{
	int res = 0;

	res = ast_custom_function_register(&geoloc_function);

	return res == 0 ? AST_MODULE_LOAD_SUCCESS : AST_MODULE_LOAD_DECLINE;
}

int geoloc_dialplan_reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

