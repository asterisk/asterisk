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

#ifndef GEOLOC_PRIVATE_H_
#define GEOLOC_PRIVATE_H_

#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/sorcery.h"
#include "asterisk/lock.h"
#include "asterisk/res_geolocation.h"

#define CONFIG_STR_TO_ENUM(_stem) \
int ast_geoloc_ ## _stem ## _str_to_enum(const char *str) \
{ \
	int i; \
	for (i = 0; i < ARRAY_LEN(_stem ## _names); i++) { \
		if (ast_strings_equal(str, _stem ## _names[i])) { \
			return i; \
		} \
	} \
	return -1; \
}

#define CONFIG_ENUM_HANDLER(_object, _stem) \
static int  _object ## _ ## _stem ## _handler(const struct aco_option *opt, struct ast_variable *var, void *obj) \
{ \
	struct ast_geoloc_ ## _object *_thisobject = obj; \
	int enumval = ast_geoloc_ ## _stem ## _str_to_enum(var->value); \
	if (enumval == -1) { \
		return -1; \
	} \
	_thisobject->_stem = enumval; \
	return 0; \
}


#define GEOLOC_ENUM_TO_NAME(_stem) \
const char * ast_geoloc_ ## _stem ## _to_name(int ix) \
{ \
	if (!ARRAY_IN_BOUNDS(ix, _stem ## _names)) { \
		return "none"; \
	} else { \
		return _stem ## _names[ix]; \
	} \
}

#define CONFIG_ENUM_TO_STR(_object, _stem) \
static int _object ## _ ## _stem ## _to_str(const void *obj, const intptr_t *args, char **buf) \
{ \
	const struct ast_geoloc_ ## _object *_thisobject = obj; \
	if (!ARRAY_IN_BOUNDS(_thisobject->_stem, _stem ## _names)) { \
		*buf = ast_strdup("none"); \
	} else { \
		*buf = ast_strdup(_stem ## _names[_thisobject->_stem]); \
	} \
	return 0; \
}

#define CONFIG_ENUM(_object, _stem) \
CONFIG_STR_TO_ENUM(_stem) \
GEOLOC_ENUM_TO_NAME(_stem) \
CONFIG_ENUM_HANDLER(_object, _stem) \
CONFIG_ENUM_TO_STR(_object, _stem)

#define CONFIG_VAR_LIST_HANDLER(_object, _stem) \
static int  _object ## _ ## _stem ## _handler(const struct aco_option *opt, struct ast_variable *var, void *obj) \
{ \
	struct ast_geoloc_ ## _object *_thisobject = obj; \
	struct ast_variable *new_var; \
	char *item_string, *item, *item_name, *item_value; \
	int rc = 0;\
	if (ast_strlen_zero(var->value)) { return 0; } \
	item_string = ast_strdupa(var->value); \
	while ((item = ast_strsep(&item_string, ',', AST_STRSEP_ALL))) { \
		item_name = ast_strsep(&item, '=', AST_STRSEP_ALL); \
		item_value = ast_strsep(&item, '=', AST_STRSEP_ALL); \
		new_var = ast_variable_new(item_name, S_OR(item_value, ""), ""); \
		if (!new_var) { \
			rc = -1; \
			break; \
		} \
		ast_variable_list_append(&_thisobject->_stem, new_var); \
	} \
	return rc; \
}

#define CONFIG_VAR_LIST_DUP(_object, _stem) \
static int  _object ## _ ## _stem ## _dup(const void *obj, struct ast_variable **fields) \
{ \
	const struct ast_geoloc_ ## _object *_thisobject = obj; \
	if (_thisobject->_stem) { \
		*fields = ast_variables_dup(_thisobject->_stem); \
	} \
	return 0; \
}

#define CONFIG_VAR_LIST_TO_STR(_object, _stem) \
static int  _object ## _ ## _stem ## _to_str(const void *obj, const intptr_t *args, char **buf) \
{ \
	const struct ast_geoloc_ ## _object *_thisobject = obj; \
	struct ast_str *str = ast_variable_list_join(_thisobject->_stem, ",", "=", "\"", NULL); \
	*buf = ast_strdup(ast_str_buffer(str)); \
	ast_free(str); \
	return 0; \
}

#define CONFIG_VAR_LIST(_object, _stem) \
CONFIG_VAR_LIST_HANDLER(_object, _stem) \
CONFIG_VAR_LIST_DUP(_object, _stem) \
CONFIG_VAR_LIST_TO_STR(_object, _stem)

int geoloc_config_load(void);
int geoloc_config_reload(void);
int geoloc_config_unload(void);

struct ast_xml_node *geoloc_civicaddr_list_to_xml(const struct ast_variable *resolved_location,
	const char *ref_string);
int geoloc_civicaddr_load(void);
int geoloc_civicaddr_unload(void);
int geoloc_civicaddr_reload(void);

struct ast_xml_node *geoloc_gml_list_to_xml(const struct ast_variable *resolved_location,
	const char *ref_string);
int geoloc_gml_unload(void);
int geoloc_gml_load(void);
int geoloc_gml_reload(void);

int geoloc_dialplan_unload(void);
int geoloc_dialplan_load(void);
int geoloc_dialplan_reload(void);

int geoloc_channel_unload(void);
int geoloc_channel_load(void);
int geoloc_channel_reload(void);

int geoloc_eprofile_unload(void);
int geoloc_eprofile_load(void);
int geoloc_eprofile_reload(void);

struct ast_sorcery *geoloc_get_sorcery(void);

struct ast_variable *geoloc_eprofile_resolve_varlist(struct ast_variable *source,
	struct ast_variable *variables, struct ast_channel *chan);


#endif /* GEOLOC_PRIVATE_H_ */
