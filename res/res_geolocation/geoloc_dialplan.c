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
	struct ast_datastore *ds;
	struct ast_geoloc_eprofile *eprofile = NULL;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(field);
	);

	/* Check for zero arguments */
	if (ast_strlen_zero(parsed_data)) {
		ast_log(LOG_ERROR, "%s: Cannot call without arguments\n", cmd);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-1");
		return 0;
	}

	AST_STANDARD_APP_ARGS(args, parsed_data);

	if (ast_strlen_zero(args.field)) {
		ast_log(LOG_ERROR, "%s: Cannot call without a field to query\n", cmd);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-1");
		return 0;
	}

	ds = ast_geoloc_datastore_find(chan);
	if (!ds) {
		ast_log(LOG_NOTICE, "%s: There is no geoloc profile on this channel\n", cmd);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-2");
		return 0;
	}

	eprofile = ast_geoloc_datastore_get_eprofile(ds, 0);
	if (!eprofile) {
		ast_log(LOG_NOTICE, "%s: There is no geoloc profile on this channel\n", cmd);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-2");
		return 0;
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
		varlist_to_str(eprofile->location_info, buf, len);
	} else if (ast_strings_equal(args.field, "location_info_refinement")) {
		varlist_to_str(eprofile->location_refinement, buf, len);
	} else if (ast_strings_equal(args.field, "location_variables")) {
		varlist_to_str(eprofile->location_variables, buf, len);
	} else if (ast_strings_equal(args.field, "effective_location")) {
		varlist_to_str(eprofile->effective_location, buf, len);
	} else if (ast_strings_equal(args.field, "usage_rules")) {
		varlist_to_str(eprofile->usage_rules, buf, len);
	} else if (ast_strings_equal(args.field, "confidence")) {
		varlist_to_str(eprofile->confidence, buf, len);
	} else {
		ast_log(LOG_ERROR, "%s: Field '%s' is not valid\n", cmd, args.field);
		pbx_builtin_setvar_helper(chan, "GEOLOCPROFILESTATUS", "-3");
	}

	ao2_ref(eprofile, -1);
	return 0;
}

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
	ast_variables_destroy(_ep->_field); \
	_ep->_field = _list; \
})

static int geoloc_profile_write(struct ast_channel *chan, const char *cmd, char *data,
	 const char *value)
{
	char *parsed_data = ast_strdupa(data);
	const char *chan_name = ast_channel_name(chan);
	struct ast_datastore *ds; /* Reminder: datastores aren't ao2 objects */
	RAII_VAR(struct ast_geoloc_eprofile *, eprofile, NULL, ao2_cleanup);

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(field);
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

	} else if (ast_strings_equal(args.field, "profile_precedence")) {
		TEST_ENUM_VALUE(chan_name, eprofile, precedence, value);

	} else if (ast_strings_equal(args.field, "format")) {
		TEST_ENUM_VALUE(chan_name, eprofile, format, value);

	} else if (ast_strings_equal(args.field, "pidf_element")) {
		TEST_ENUM_VALUE(chan_name, eprofile, pidf_element, value);

	} else if (ast_strings_equal(args.field, "location_info")) {
		TEST_VARLIST(chan_name, eprofile, location_info, value);
	} else if (ast_strings_equal(args.field, "location_source")) {
		ast_string_field_set(eprofile, location_source, value);
	} else if (ast_strings_equal(args.field, "location_info_refinement")) {
		TEST_VARLIST(chan_name, eprofile, location_refinement, value);
	} else if (ast_strings_equal(args.field, "location_variables")) {
		TEST_VARLIST(chan_name, eprofile, location_variables, value);
	} else if (ast_strings_equal(args.field, "effective_location")) {
		TEST_VARLIST(chan_name, eprofile, effective_location, value);
	} else if (ast_strings_equal(args.field, "usage_rules")) {
		TEST_VARLIST(chan_name, eprofile, usage_rules, value);
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

