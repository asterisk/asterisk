/*
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
#include "asterisk/res_geolocation.h"
#include "geoloc_private.h"

struct geoloc_gml_attr {
	const char *name;
	int min_required;
	int max_allowed;
	int (*validator)(const char *name, const char *value, const struct ast_variable *varlist,
		char **result);
};

#define MAX_SHAPE_ATTRIBUTES 9
struct geoloc_gml_shape_def {
	const char *shape_type;
	const char *crs;
	struct geoloc_gml_attr required_attributes[MAX_SHAPE_ATTRIBUTES];
};

#define SET_RESULT(__result, ...) \
({ \
	if (__result) { \
		__ast_asprintf(__FILE__, __LINE__, __PRETTY_FUNCTION__, result,  __VA_ARGS__); \
	} \
})

static int crs_validator(const char *name, const char *value, const struct ast_variable *varlist,
	char **result)
{
	if (!ast_strings_equal(value, "2d") && !ast_strings_equal(value, "3d")) {
		SET_RESULT(result, "Invalid crs '%s'.  Must be either '2d' or '3d'", value);
		return 0;
	}
	return 1;
}

static int pos_validator(const char *name, const char *value, const struct ast_variable *varlist,
	char **result)
{
	const char *crs = S_OR(ast_variable_find_in_list(varlist, "crs"), "2d");
	float lat;
	float lon;
	float alt;
	int count;

	count = sscanf(value, "%f %f %f", &lat, &lon, &alt);
	if (ast_strings_equal(crs, "3d") && count != 3) {
		SET_RESULT(result, "Invalid 3d position '%s'.  Must be 3 floating point values.", value);
		return 0;
	}
	if (ast_strings_equal(crs, "2d") && count != 2) {
		SET_RESULT(result, "Invalid 2d position '%s'.  Must be 2 floating point values.", value);
		return 0;
	}
	return 1;
}

static int float_validator(const char *name, const char *value, const struct ast_variable *varlist,
	char **result)
{
	float val;
	if (sscanf(value, "%f", &val) != 1) {
		SET_RESULT(result, "Invalid floating point value '%s' in '%s'.", value, name);
		return 0;
	}
	return 1;
}

enum angle_parse_result {
	ANGLE_PARSE_RESULT_SUCCESS = 0,
	ANGLE_PARSE_ERROR_NO_ANGLE,
	ANGLE_PARSE_ERROR_INVALID_ANGLE,
	ANGLE_PARSE_ERROR_ANGLE_OUT_OF_RANGE,
	ANGLE_PARSE_ERROR_INVALID_UOM,
};

static enum angle_parse_result angle_parser(const char *name, const char *value,
	char **angle, char **uom, char **result)
{
	char *tmp_angle = NULL;
	char *tmp_uom = NULL;
	float f_angle;
	char *junk;
	char *work = ast_strdupa(value);

	tmp_angle = ast_strsep(&work, ' ', AST_STRSEP_ALL);
	if (ast_strlen_zero(tmp_angle)) {
		SET_RESULT(result, "Empty angle in '%s'", name);
		return ANGLE_PARSE_ERROR_NO_ANGLE;
	}
	f_angle = strtof(tmp_angle, &junk);
	if (!ast_strlen_zero(junk)) {
		SET_RESULT(result, "Invalid angle '%s' in '%s'", value, name);
		return ANGLE_PARSE_ERROR_INVALID_ANGLE;
	}

	tmp_uom = ast_strsep(&work, ' ', AST_STRSEP_ALL);
	if (ast_strlen_zero(tmp_uom)) {
		tmp_uom = "degrees";
	}

	if (ast_begins_with(tmp_uom, "deg")) {
		tmp_uom = "degrees";
	} else if (ast_begins_with(tmp_uom, "rad")) {
		tmp_uom = "radians";
	} else {
		SET_RESULT(result, "Invalid UOM '%s' in '%s'.  Must be 'degrees' or 'radians'.", value, name);
		return ANGLE_PARSE_ERROR_INVALID_UOM;
	}

	if (ast_strings_equal(tmp_uom, "degrees") && f_angle > 360.0) {
		SET_RESULT(result, "Angle '%s' must be <= 360.0 for UOM '%s' in '%s'", tmp_angle, tmp_uom, name);
		return ANGLE_PARSE_ERROR_ANGLE_OUT_OF_RANGE;
	}

	if (ast_strings_equal(tmp_uom, "radians") && f_angle > 100.0) {
		SET_RESULT(result, "Angle '%s' must be <= 100.0 for UOM '%s' in '%s'", tmp_angle, tmp_uom, name);
		return ANGLE_PARSE_ERROR_ANGLE_OUT_OF_RANGE;
	}

	if (angle) {
		*angle = ast_strdup(tmp_angle);
	}
	if (uom) {
		*uom = ast_strdup(tmp_uom);
	}
	return ANGLE_PARSE_RESULT_SUCCESS;
}

static int angle_validator(const char *name, const char *value, const struct ast_variable *varlist,
	char **result)
{
	enum angle_parse_result rc = angle_parser(name, value, NULL, NULL, result);

	return rc == ANGLE_PARSE_RESULT_SUCCESS;
}

#define _SENTRY {NULL, -1, -1, NULL}

#define CRS_OPT {"crs", 0, 1, crs_validator}
#define CRS_REQ {"crs", 1, 1, crs_validator}

static struct geoloc_gml_shape_def gml_shape_defs[] = {
	{ "Point", "any", { CRS_OPT, {"pos", 1, 1, pos_validator}, _SENTRY }},
	{ "Polygon", "any", { CRS_OPT, {"pos", 3, -1, pos_validator}, _SENTRY }},
	{ "Circle", "2d", { CRS_OPT, {"pos", 1, 1, pos_validator}, {"radius", 1, 1, float_validator}, _SENTRY }},
	{ "Ellipse", "2d", { CRS_OPT, {"pos", 1, 1, pos_validator}, {"semiMajorAxis", 1, 1, float_validator},
		{"semiMinorAxis", 1, 1, float_validator}, {"orientation", 1, 1, angle_validator}, _SENTRY }},
	{ "ArcBand", "2d", { CRS_OPT, {"pos", 1, 1, pos_validator}, {"innerRadius", 1, 1, float_validator},
		{"outerRadius", 1, 1, float_validator}, {"startAngle", 1, 1, angle_validator},
		{"openingAngle", 1, 1, angle_validator},
		_SENTRY }},
	{ "Sphere", "3d", { CRS_REQ, {"pos", 1, 1, pos_validator}, {"radius", 1, 1, float_validator}, _SENTRY }},
	{ "Ellipsoid", "3d", { CRS_REQ, {"pos", 1, 1, pos_validator}, {"semiMajorAxis", 1, 1, float_validator},
		{"semiMinorAxis", 1, 1, float_validator}, {"verticalAxis", 1, 1, float_validator},
		{"orientation", 1, 1, angle_validator}, _SENTRY }},
	{ "Prism", "3d", { CRS_REQ, {"pos", 3, -1, pos_validator}, {"height", 1, 1, float_validator}, _SENTRY }},
};

static int find_shape_index(const char *shape)
{
	int i = 0;
	int shape_count = ARRAY_LEN(gml_shape_defs);

	for (i = 0; i < shape_count; i++) {
		if (ast_strings_equal(shape, gml_shape_defs[i].shape_type)) {
			return i;
		}
	}
	return -1;
}

static int find_attribute_index(int shape_index, const char *name)
{
	int i = 0;

	for (i = 0; i <  MAX_SHAPE_ATTRIBUTES; i++) {
		if (gml_shape_defs[shape_index].required_attributes[i].name == NULL) {
			return -1;
		}
		if (ast_strings_equal(name, gml_shape_defs[shape_index].required_attributes[i].name)) {
			return i;
		}
	}
	return -1;
}

static enum ast_geoloc_validate_result validate_def_varlist(int shape_index, const struct ast_variable *varlist,
	char **result)
{
	const struct ast_variable *var;
	int i;

	for (var = varlist; var; var = var->next) {
		int vname_index = -1;
		if (ast_strings_equal("shape", var->name)) {
			continue;
		}

		vname_index = find_attribute_index(shape_index, var->name);
		if (vname_index < 0) {
			SET_RESULT(result, "Invalid variable name '%s'\n", var->name);
			return AST_GEOLOC_VALIDATE_INVALID_VARNAME;
		}
		if (!gml_shape_defs[shape_index].required_attributes[vname_index].validator(var->name, var->value,
			varlist, result)) {
			return AST_GEOLOC_VALIDATE_INVALID_VALUE;
		}
	}

	for (i = 0; i < ARRAY_LEN(gml_shape_defs[shape_index].required_attributes); i++) {
		int count = 0;
		if (gml_shape_defs[shape_index].required_attributes[i].name == NULL) {
			break;
		}

		for (var = varlist; var; var = var->next) {
			if (ast_strings_equal(gml_shape_defs[shape_index].required_attributes[i].name, var->name)) {
				count++;
			}
		}
		if (count < gml_shape_defs[shape_index].required_attributes[i].min_required) {
			SET_RESULT(result, "Number of '%s' variables %d is < %d",
				gml_shape_defs[shape_index].required_attributes[i].name,
				count,
				gml_shape_defs[shape_index].required_attributes[i].min_required);
			return AST_GEOLOC_VALIDATE_NOT_ENOUGH_VARNAMES;
		}
		if (gml_shape_defs[shape_index].required_attributes[i].max_allowed > 0 &&
			count > gml_shape_defs[shape_index].required_attributes[i].max_allowed) {
			SET_RESULT(result, "Number of '%s' variables %d is > %d",
				gml_shape_defs[shape_index].required_attributes[i].name,
				count,
				gml_shape_defs[shape_index].required_attributes[i].max_allowed);
			return AST_GEOLOC_VALIDATE_TOO_MANY_VARNAMES;
		}
	}

	return AST_GEOLOC_VALIDATE_SUCCESS;
}

enum ast_geoloc_validate_result ast_geoloc_gml_validate_varlist(struct ast_variable *varlist,
	char **result)
{
	const char *shape_type = ast_variable_find_in_list(varlist, "shape");
	int shape_index = -1;
	const char *crs = ast_variable_find_in_list(varlist, "crs");

	if (!shape_type) {
		SET_RESULT(result, "Missing 'shape'");
		return AST_GEOLOC_VALIDATE_MISSING_SHAPE;
	}

	shape_index = find_shape_index(shape_type);
	if (shape_index < 0) {
		SET_RESULT(result, "Invalid shape '%s'", shape_type);
		return AST_GEOLOC_VALIDATE_INVALID_SHAPE;
	}

	if (ast_strlen_zero(crs)) {
		struct ast_variable *vcrs = NULL;
		if (ast_strings_equal("any", gml_shape_defs[shape_index].crs)) {
			crs = "2d";
		} else {
			crs = gml_shape_defs[shape_index].crs;
		}

		vcrs = ast_variable_new("crs", "2d", "");
		if (vcrs) {
			ast_variable_list_append(&varlist, vcrs);
		}
	}
	if (!crs_validator("crs", crs, varlist, result)) {
		return AST_GEOLOC_VALIDATE_INVALID_CRS;
	}

	if (!ast_strings_equal("any", gml_shape_defs[shape_index].crs)
		&& !ast_strings_equal(crs, gml_shape_defs[shape_index].crs)) {
		SET_RESULT(result, "Invalid crs '%s' for shape '%s'", crs, shape_type);
		return AST_GEOLOC_VALIDATE_INVALID_CRS_FOR_SHAPE;
	}

	return validate_def_varlist(shape_index, varlist, result);
}

static char *handle_gml_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "geoloc show gml_shape_defs";
		e->usage =
			"Usage: geoloc show gml_shape_defs\n"
			"       Show the GML Shape definitions.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "%-16s %-3s %-32s\n", "Shape", "CRS", "Attributes name(min,max)");
	ast_cli(a->fd, "================ === ===============================\n");

	for (i = 0; i < ARRAY_LEN(gml_shape_defs); i++) {
		int j;
		ast_cli(a->fd, "%-16s %-3s", gml_shape_defs[i].shape_type, gml_shape_defs[i].crs);
		for (j = 0; j < ARRAY_LEN(gml_shape_defs[i].required_attributes); j++) {
			if (gml_shape_defs[i].required_attributes[j].name == NULL) {
				break;
			}
			if (gml_shape_defs[i].required_attributes[j].max_allowed >= 0) {
				ast_cli(a->fd, " %s(%d,%d)", gml_shape_defs[i].required_attributes[j].name,
					gml_shape_defs[i].required_attributes[j].min_required,
					gml_shape_defs[i].required_attributes[j].max_allowed);
			} else {
				ast_cli(a->fd, " %s(%d,unl)", gml_shape_defs[i].required_attributes[j].name,
					gml_shape_defs[i].required_attributes[j].min_required);
			}
		}
		ast_cli(a->fd, "\n");
	}
	ast_cli(a->fd, "\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry geoloc_gml_cli[] = {
	AST_CLI_DEFINE(handle_gml_show, "Show the GML Shape definitions"),
};

struct ast_xml_node *geoloc_gml_list_to_xml(struct ast_variable *resolved_location,
	const char *ref_string)
{
	const char *shape;
	const char *crs;
	struct ast_variable *var;
	struct ast_xml_node *gml_node;
	struct ast_xml_node *child_node;
	enum ast_geoloc_validate_result res;
	RAII_VAR(char *, result, NULL, ast_free);
	int rc = 0;

	SCOPE_ENTER(3, "%s", ref_string);

	if (!resolved_location) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: resolved_location was NULL\n",
			ref_string);
	}

	res = ast_geoloc_gml_validate_varlist(resolved_location, &result);
	if (res != AST_GEOLOC_VALIDATE_SUCCESS) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: %s\n",
			ref_string, result);
	}

	shape = ast_variable_find_in_list(resolved_location, "shape");
	if (ast_strlen_zero(shape)) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: There's no 'shape' parameter\n",
			ref_string);
	}

	crs = ast_variable_find_in_list(resolved_location, "crs");
	if (ast_strlen_zero(crs)) {
		struct ast_variable *vcrs = ast_variable_new("crs", "2d", "");
		if (vcrs) {
			ast_variable_list_append(&resolved_location, vcrs);
		}
		crs = "2d";
	}

	gml_node = ast_xml_new_node(shape);
	if (!gml_node) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create '%s' XML node\n", shape, ref_string);
	}
	rc = ast_xml_set_attribute(gml_node, "crs", crs);
	if (rc != 0) {
		ast_xml_free_node(gml_node);
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create 'crs' XML attribute\n", ref_string);
	}

	for (var = (struct ast_variable *)resolved_location; var; var = var->next) {

		if (ast_strings_equal(var->name, "shape") || ast_strings_equal(var->name, "crs")) {
			continue;
		}

		child_node = ast_xml_new_child(gml_node, var->name);
		if (!child_node) {
			ast_xml_free_node(gml_node);
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create '%s' XML node\n", var->name, ref_string);
		}

		if (ast_strings_equal(var->name, "orientation") || ast_strings_equal(var->name, "startAngle")
			|| ast_strings_equal(var->name, "openingAngle")) {
			RAII_VAR(char *, angle, NULL, ast_free);
			RAII_VAR(char *, uom, NULL, ast_free);

			enum angle_parse_result rc = angle_parser(var->name, var->value, &angle, &uom, &result);
			if (rc != ANGLE_PARSE_RESULT_SUCCESS) {
				ast_xml_free_node(gml_node);
				SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: %s\n", ref_string, result);
			}
			rc = ast_xml_set_attribute(child_node, "uom", uom);
			if (rc != 0) {
				ast_xml_free_node(gml_node);
				SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create 'uom' XML attribute\n", ref_string);
			}
			ast_xml_set_text(child_node, angle);
		} else {
			ast_xml_set_text(child_node, var->value);
		}
	}

	SCOPE_EXIT_RTN_VALUE(gml_node, "%s: Done\n", ref_string);
}

int geoloc_gml_unload(void)
{
	ast_cli_unregister_multiple(geoloc_gml_cli, ARRAY_LEN(geoloc_gml_cli));

	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_gml_load(void)
{
	ast_cli_register_multiple(geoloc_gml_cli, ARRAY_LEN(geoloc_gml_cli));

	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_gml_reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}
