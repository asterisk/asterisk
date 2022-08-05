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


#if 1 //not used yet.
enum geoloc_shape_attrs {
	GEOLOC_SHAPE_ATTR_POS = 0,
	GEOLOC_SHAPE_ATTR_POS3D,
	GEOLOC_SHAPE_ATTR_RADIUS,
	GEOLOC_SHAPE_ATTR_SEMI_MAJOR_AXIS,
	GEOLOC_SHAPE_ATTR_SEMI_MINOR_AXIS,
	GEOLOC_SHAPE_ATTR_VERTICAL_AXIS,
	GEOLOC_SHAPE_ATTR_HEIGHT,
	GEOLOC_SHAPE_ATTR_ORIENTATION,
	GEOLOC_SHAPE_ATTR_ORIENTATION_UOM,
	GEOLOC_SHAPE_ATTR_INNER_RADIUS,
	GEOLOC_SHAPE_ATTR_OUTER_RADIUS,
	GEOLOC_SHAPE_ATTR_STARTING_ANGLE,
	GEOLOC_SHAPE_ATTR_OPENING_ANGLE,
	GEOLOC_SHAPE_ATTR_ANGLE_UOM,
};

struct geoloc_gml_attr_def {
	enum geoloc_shape_attrs attr;
	const char *name;
	int (*validator)(const char *value);
	int (*transformer)(struct ast_variable *value);
};

struct geoloc_gml_attr_def gml_attr_defs[] = {
	{ GEOLOC_SHAPE_ATTR_POS, "pos", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_POS3D,"pos3d", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_RADIUS,"radius", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_SEMI_MAJOR_AXIS,"semiMajorAxis", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_SEMI_MINOR_AXIS,"semiMinorAxis", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_VERTICAL_AXIS,"verticalAxis", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_HEIGHT,"height", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_ORIENTATION,"orientation", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_ORIENTATION_UOM,"orientation_uom", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_INNER_RADIUS,"innerRadius", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_OUTER_RADIUS,"outerRadius", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_STARTING_ANGLE,"startingAngle", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_OPENING_ANGLE,"openingAngle", NULL, NULL},
	{ GEOLOC_SHAPE_ATTR_ANGLE_UOM,"angle_uom", NULL, NULL},
};
#endif  //not used yet.

struct geoloc_gml_attr {
	const char *attribute;
	int min_required;
	int max_allowed;
	int (*validator)(const char *value);
};

struct geoloc_gml_shape_def {
	const char *shape_type;
	struct geoloc_gml_attr required_attributes[8];
};

static int pos_validator(const char *value)
{
	float lat;
	float lon;
	return (sscanf(value, "%f %f", &lat, &lon) == 2);
}

static int pos3d_validator(const char *value)
{
	float lat;
	float lon;
	float alt;
	return (sscanf(value, "%f %f %f", &lat, &lon, &alt) == 3);
}

static int float_validator(const char *value)
{
	float val;
	return (sscanf(value, "%f", &val) == 1);
}

static int uom_validator(const char *value)
{
	return (ast_strings_equal(value, "degrees") || ast_strings_equal(value, "radians"));
}


static struct geoloc_gml_shape_def gml_shape_defs[8] = {
	{ "Point", { {"pos", 1, 1, pos_validator}, {NULL, -1, -1} }},
	{ "Polygon", { {"pos", 3, -1, pos_validator}, {NULL, -1, -1} }},
	{ "Circle", { {"pos", 1, 1, pos_validator}, {"radius", 1, 1, float_validator},{NULL, -1, -1}}},
	{ "Ellipse", { {"pos", 1, 1, pos_validator}, {"semiMajorAxis", 1, 1, float_validator},
		{"semiMinorAxis", 1, 1, float_validator}, {"orientation", 1, 1, float_validator},
		{"orientation_uom", 1, 1, uom_validator}, {NULL, -1, -1} }},
	{ "ArcBand", { {"pos", 1, 1, pos_validator}, {"innerRadius", 1, 1, float_validator},
		{"outerRadius", 1, 1, float_validator}, {"startAngle", 1, 1, float_validator},
		{"startAngle_uom", 1, 1, uom_validator}, {"openingAngle", 1, 1, float_validator},
		{"openingAngle_uom", 1, 1, uom_validator}, {NULL, -1, -1} }},
	{ "Sphere", { {"pos3d", 1, 1, pos3d_validator}, {"radius", 1, 1, float_validator}, {NULL, -1, -1} }},
	{ "Ellipse", { {"pos3d", 1, 1, pos3d_validator}, {"semiMajorAxis", 1, 1, float_validator},
		{"semiMinorAxis", 1, 1, float_validator}, {"verticalAxis", 1, 1, float_validator},
		{"orientation", 1, 1, float_validator}, {"orientation_uom", 1, 1, uom_validator}, {NULL, -1, -1} }},
	{ "Prism", { {"pos3d", 3, -1, pos_validator}, {"height", 1, 1, float_validator}, {NULL, -1, -1} }},
};

enum ast_geoloc_validate_result ast_geoloc_gml_validate_varlist(const struct ast_variable *varlist,
	const char **result)
{
	int def_index = -1;
	const struct ast_variable *var;
	int i;
	const char *shape_type = ast_variable_find_in_list(varlist, "shape");

	if (!shape_type) {
		return AST_GEOLOC_VALIDATE_MISSING_SHAPE;
	}

	for (i = 0; i < ARRAY_LEN(gml_shape_defs); i++) {
		if (ast_strings_equal(gml_shape_defs[i].shape_type, shape_type)) {
			def_index = i;
		}
	}
	if (def_index < 0) {
		return AST_GEOLOC_VALIDATE_INVALID_SHAPE;
	}

	for (var = varlist; var; var = var->next) {
		int vname_index = -1;
		if (ast_strings_equal("shape", var->name)) {
			continue;
		}
		for (i = 0; i < ARRAY_LEN(gml_shape_defs[def_index].required_attributes); i++) {
			if (gml_shape_defs[def_index].required_attributes[i].attribute == NULL) {
				break;
			}
			if (ast_strings_equal(gml_shape_defs[def_index].required_attributes[i].attribute, var->name)) {
				vname_index = i;
				break;
			}
		}
		if (vname_index < 0) {
			*result = var->name;
			return AST_GEOLOC_VALIDATE_INVALID_VARNAME;
		}
		if (!gml_shape_defs[def_index].required_attributes[vname_index].validator(var->value)) {
			*result = var->name;
			return AST_GEOLOC_VALIDATE_INVALID_VALUE;
		}
	}

	for (i = 0; i < ARRAY_LEN(gml_shape_defs[def_index].required_attributes); i++) {
		int count = 0;
		if (gml_shape_defs[def_index].required_attributes[i].attribute == NULL) {
			break;
		}

		for (var = varlist; var; var = var->next) {
			if (ast_strings_equal(gml_shape_defs[def_index].required_attributes[i].attribute, var->name)) {
				count++;
			}
		}
		if (count < gml_shape_defs[def_index].required_attributes[i].min_required) {
			*result = gml_shape_defs[def_index].required_attributes[i].attribute;
			return AST_GEOLOC_VALIDATE_NOT_ENOUGH_VARNAMES;
		}
		if (gml_shape_defs[def_index].required_attributes[i].max_allowed > 0 &&
			count > gml_shape_defs[def_index].required_attributes[i].max_allowed) {
			*result = gml_shape_defs[def_index].required_attributes[i].attribute;
			return AST_GEOLOC_VALIDATE_TOO_MANY_VARNAMES;
		}
	}
	return AST_GEOLOC_VALIDATE_SUCCESS;
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

	ast_cli(a->fd, "%-16s %-32s\n", "Shape", "Attributes name(min,max)");
	ast_cli(a->fd, "================ ===============================\n");

	for (i = 0; i < ARRAY_LEN(gml_shape_defs); i++) {
		int j;
		ast_cli(a->fd, "%-16s", gml_shape_defs[i].shape_type);
		for (j = 0; j < ARRAY_LEN(gml_shape_defs[i].required_attributes); j++) {
			if (gml_shape_defs[i].required_attributes[j].attribute == NULL) {
				break;
			}
			if (gml_shape_defs[i].required_attributes[j].max_allowed >= 0) {
				ast_cli(a->fd, " %s(%d,%d)", gml_shape_defs[i].required_attributes[j].attribute,
					gml_shape_defs[i].required_attributes[j].min_required,
					gml_shape_defs[i].required_attributes[j].max_allowed);
			} else {
				ast_cli(a->fd, " %s(%d,unl)", gml_shape_defs[i].required_attributes[j].attribute,
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

struct ast_xml_node *geoloc_gml_list_to_xml(const struct ast_variable *resolved_location,
	const char *ref_string)
{
	const char *shape;
	char *crs;
	struct ast_variable *var;
	struct ast_xml_node *gml_node;
	struct ast_xml_node *child_node;
	int rc = 0;

	SCOPE_ENTER(3, "%s", ref_string);

	if (!resolved_location) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: resolved_location was NULL\n",
			ref_string);
	}

	shape = ast_variable_find_in_list(resolved_location, "shape");
	if (ast_strlen_zero(shape)) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: There's no 'shape' parameter\n",
			ref_string);
	}
	crs = (char *)ast_variable_find_in_list(resolved_location, "crs");
	if (ast_strlen_zero(crs)) {
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
		RAII_VAR(char *, value, NULL, ast_free);
		char *uom = NULL;

		if (ast_strings_equal(var->name, "shape") || ast_strings_equal(var->name, "crs")) {
			continue;
		}
		value = ast_strdup(var->value);

		if (ast_strings_equal(var->name, "orientation") || ast_strings_equal(var->name, "startAngle")
			|| ast_strings_equal(var->name, "openingAngle")) {
			char *a = NULL;
			char *junk = NULL;
			float angle;
			uom = value;

			/* 'a' should now be the angle and 'uom' should be the uom */
			a = strsep(&uom, " ");
			angle = strtof(a, &junk);
			/*
			 * strtof sets junk to the first non-valid character so if it's
			 * not empty after the conversion, there were unrecognized
			 * characters in the angle.  It'll point to the NULL terminator
			 * if angle was completely converted.
			 */
			if (!ast_strlen_zero(junk)) {
				ast_xml_free_node(gml_node);
				SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: The angle portion of parameter '%s' ('%s') is malformed\n",
					ref_string, var->name, var->value);
			}

			if (ast_strlen_zero(uom)) {
				uom = "degrees";
			}

			if (ast_begins_with(uom, "deg")) {
				if (angle > 360.0) {
					ast_xml_free_node(gml_node);
					SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Parameter '%s': '%s' is malformed. "
						"Degrees can't be > 360.0\n",
						ref_string, var->name, var->value);
				}
			} else if (ast_begins_with(uom, "rad")) {
				if(angle > 100.0) {
					ast_xml_free_node(gml_node);
					SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Parameter '%s': '%s' is malformed. "
						"Radians can't be  > 100.0\n",
						ref_string, var->name, var->value);
				}
			} else {
				ast_xml_free_node(gml_node);
				SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Parameter '%s': '%s' is malformed. "
					"The unit of measure must be 'deg[rees]' or 'rad[ians]'\n",
					ref_string, var->name, var->value);
			}
		}

		child_node = ast_xml_new_child(gml_node, var->name);
		if (!child_node) {
			ast_xml_free_node(gml_node);
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create '%s' XML node\n", var->name, ref_string);
		}
		if (!ast_strlen_zero(uom)) {
			rc = ast_xml_set_attribute(child_node, "uom", uom);
			if (rc != 0) {
				ast_xml_free_node(gml_node);
				SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create 'uom' XML attribute\n", ref_string);
			}
		}
		ast_xml_set_text(child_node, value);
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
