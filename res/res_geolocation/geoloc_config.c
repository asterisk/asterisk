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
#include "asterisk/module.h"
#include "asterisk/cli.h"
#define AST_API_MODULE
#include "geoloc_private.h"

static struct ast_sorcery *geoloc_sorcery;

static const char *pidf_element_names[] = {
	"<none>",
	"device",
	"tuple",
	"person"
};

static const char *format_names[] = {
	"<none>",
	"civicAddress",
	"GML",
	"URI",
};

static const char * precedence_names[] = {
	"prefer_incoming",
	"prefer_config",
	"discard_incoming",
	"discard_config",
};

CONFIG_ENUM(location, format)
CONFIG_VAR_LIST(location, location_info)
CONFIG_VAR_LIST(location, confidence)

static void geoloc_location_destructor(void *obj) {
	struct ast_geoloc_location *location = obj;

	ast_string_field_free_memory(location);
	ast_variables_destroy(location->location_info);
	ast_variables_destroy(location->confidence);
}

static void *geoloc_location_alloc(const char *name)
{
	struct ast_geoloc_location *location = ast_sorcery_generic_alloc(sizeof(struct ast_geoloc_location), geoloc_location_destructor);
	if (location) {
		ast_string_field_init(location, 128);
	}

	return location;
}

CONFIG_ENUM(profile, pidf_element)
CONFIG_ENUM(profile, precedence)
CONFIG_VAR_LIST(profile, location_refinement)
CONFIG_VAR_LIST(profile, location_variables)
CONFIG_VAR_LIST(profile, usage_rules)

CONFIG_ENUM_HANDLER(profile, format)
CONFIG_ENUM_TO_STR(profile, format)
CONFIG_VAR_LIST(profile, location_info)
CONFIG_VAR_LIST(profile, confidence)

static void geoloc_profile_destructor(void *obj) {
	struct ast_geoloc_profile *profile = obj;

	ast_string_field_free_memory(profile);
	ast_variables_destroy(profile->location_refinement);
	ast_variables_destroy(profile->location_variables);
	ast_variables_destroy(profile->usage_rules);
	ast_variables_destroy(profile->location_info);
	ast_variables_destroy(profile->confidence);
}

static void *geoloc_profile_alloc(const char *name)
{
	struct ast_geoloc_profile *profile = ast_sorcery_generic_alloc(sizeof(*profile),
		geoloc_profile_destructor);
	if (profile) {
		ast_string_field_init(profile, 128);
	}

	return profile;
}

static enum ast_geoloc_validate_result validate_location_info(const char *id,
	enum ast_geoloc_format format, struct ast_variable *location_info)
{
	enum ast_geoloc_validate_result result;
	const char *failed;
	const char *uri;

	switch (format) {
	case AST_GEOLOC_FORMAT_NONE:
	case AST_GEOLOC_FORMAT_LAST:
		ast_log(LOG_ERROR, "Location '%s' must have a format\n", id);
		return -1;
	case AST_GEOLOC_FORMAT_CIVIC_ADDRESS:
		result = ast_geoloc_civicaddr_validate_varlist(location_info, &failed);
		if (result != AST_GEOLOC_VALIDATE_SUCCESS) {
			ast_log(LOG_ERROR, "Location '%s' has invalid item '%s' in the location\n",
				id, failed);
			return result;
		}
		break;
	case AST_GEOLOC_FORMAT_GML:
		result = ast_geoloc_gml_validate_varlist(location_info, &failed);
		if (result != AST_GEOLOC_VALIDATE_SUCCESS) {
			ast_log(LOG_ERROR, "%s for item '%s' in location '%s'\n",
				ast_geoloc_validate_result_to_str(result),	failed, id);
			return result;
		}

		break;
	case AST_GEOLOC_FORMAT_URI:
		uri = ast_variable_find_in_list(location_info, "URI");
		if (!uri) {
			struct ast_str *str = ast_variable_list_join(location_info, ",", "=", "\"", NULL);

			ast_log(LOG_ERROR, "Geolocation location '%s' format is set to '%s' but no 'URI' was found in location parameter '%s'\n",
				id, format_names[AST_GEOLOC_FORMAT_URI], ast_str_buffer(str));
			ast_free(str);
			return AST_GEOLOC_VALIDATE_NOT_ENOUGH_VARNAMES;
		}
		break;
	}

	return AST_GEOLOC_VALIDATE_SUCCESS;
}

static int validate_location_source(const char *id, const char *location_source)
{
	if (!ast_strlen_zero(location_source)) {
		struct ast_sockaddr loc_source_addr;
		int rc = ast_sockaddr_parse(&loc_source_addr, location_source, PARSE_PORT_FORBID);
		if (rc == 1) {
			ast_log(LOG_ERROR, "Geolocation location '%s' location_source '%s' must be a FQDN."
				" RFC8787 expressly forbids IP addresses.\n",
				id, location_source);
			return -1;
		}
	}

	return 0;
}

static int geoloc_location_apply_handler(const struct ast_sorcery *sorcery, void *obj)
{
	struct ast_geoloc_location *location = obj;
	const char *location_id = ast_sorcery_object_get_id(location);
	enum ast_geoloc_validate_result result;
	int rc = 0;

	result = validate_location_info(location_id, location->format, location->location_info);
	if (result != AST_GEOLOC_VALIDATE_SUCCESS) {
		return -1;
	}

	rc = validate_location_source(location_id, location->location_source);
	if (rc != 0) {
		return -1;
	}

	return 0;
}

static int geoloc_profile_apply_handler(const struct ast_sorcery *sorcery, void *obj)
{
	struct ast_geoloc_profile *profile = obj;
	struct ast_geoloc_location *location;
	const char *id = ast_sorcery_object_get_id(profile);
	enum ast_geoloc_validate_result result;
	enum ast_geoloc_format format = AST_GEOLOC_FORMAT_NONE;
	int rc = 0;

	if (!ast_strlen_zero(profile->location_reference)) {
		if (profile->location_info ||
			profile->format != AST_GEOLOC_FORMAT_NONE) {
			ast_log(LOG_ERROR, "Profile '%s' can't have location_reference and location_info or format at the same time",
				id);
			return -1;
		}
		return 0;
	}

	if (profile->location_info) {
		result = validate_location_info(id, profile->format, profile->location_info);
		if (result != AST_GEOLOC_VALIDATE_SUCCESS) {
			return -1;
		}

		rc = validate_location_source(id, profile->location_source);
		if (rc != 0) {
			return -1;
		}

		return 0;
	}

	if (!ast_strlen_zero(profile->location_reference)) {
		location = ast_sorcery_retrieve_by_id(geoloc_sorcery, "location", profile->location_reference);
		if (!location) {
			ast_log(LOG_ERROR, "Profile '%s' has a location_reference '%s' that doesn't exist",
				id, profile->location_reference);
			return -1;
		}
		format = location->format;
		ao2_ref(location, -1);
	}

	if (profile->location_refinement) {
		result = validate_location_info(id, format, profile->location_refinement);
		if (result != AST_GEOLOC_VALIDATE_SUCCESS) {
			return -1;
		}
	}

	return 0;
}

struct ast_sorcery *geoloc_get_sorcery(void)
{
	ast_sorcery_ref(geoloc_sorcery);
	return geoloc_sorcery;
}

static char *geoloc_config_list_locations(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator iter;
	struct ao2_container *sorted_container;
	struct ao2_container *unsorted_container;
	struct ast_geoloc_location *loc;
	int using_regex = 0;
	char *result = CLI_SUCCESS;
	int ret = 0;
	int count = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "geoloc list locations";
		e->usage = "Usage: geoloc list locations [ like <pattern> ]\n"
		            "      List Geolocation Location Objects\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3 && a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 5) {
		if (strcasecmp(a->argv[3], "like")) {
			return CLI_SHOWUSAGE;
		}
		using_regex = 1;
	}

	sorted_container = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sorcery_object_id_sort, NULL);
	if (!sorted_container) {
		ast_cli(a->fd, "Geolocation Location Objects: Unable to allocate temporary container\n");
		return CLI_FAILURE;
	}

	/* Get a sorted snapshot of the scheduled tasks */
	if (using_regex) {
		unsorted_container = ast_sorcery_retrieve_by_regex(geoloc_sorcery, "location", a->argv[4]);
	} else {
		unsorted_container = ast_sorcery_retrieve_by_fields(geoloc_sorcery, "location",
			AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	}

	ret = ao2_container_dup(sorted_container, unsorted_container, 0);
	ao2_ref(unsorted_container, -1);
	if (ret != 0) {
		ao2_ref(sorted_container, -1);
		ast_cli(a->fd, "Geolocation Location Objects: Unable to sort temporary container\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Geolocation Location Objects:\n\n");

	ast_cli(a->fd,
		"<Object ID...................................> <Format.....> <Details.............>\n"
		"===================================================================================\n");

	iter = ao2_iterator_init(sorted_container, AO2_ITERATOR_UNLINK);
	for (; (loc = ao2_iterator_next(&iter)); ao2_ref(loc, -1)) {
		struct ast_str *str;

		ao2_lock(loc);
		str = ast_variable_list_join(loc->location_info, ",", "=", "\"", NULL);
		if (!str) {
			ao2_unlock(loc);
			ao2_ref(loc, -1);
			ast_cli(a->fd, "Geolocation Location Objects: Unable to allocate temp string for '%s'\n",
				ast_sorcery_object_get_id(loc));
			result = CLI_FAILURE;
			break;
		}

		ast_cli(a->fd, "%-46.46s %-13s %-s\n",
			ast_sorcery_object_get_id(loc),
			format_names[loc->format],
			ast_str_buffer(str));
		ao2_unlock(loc);
		ast_free(str);
		count++;
	}
	ao2_iterator_destroy(&iter);
	ao2_ref(sorted_container, -1);
	ast_cli(a->fd, "\nTotal Location Objects: %d\n\n", count);

	return result;
}

static char *geoloc_config_list_profiles(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator iter;
	struct ao2_container *sorted_container;
	struct ao2_container *unsorted_container;
	struct ast_geoloc_profile *profile;
	int using_regex = 0;
	char *result = CLI_SUCCESS;
	int ret = 0;
	int count = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "geoloc list profiles";
		e->usage = "Usage: geoloc list profiles [ like <pattern> ]\n"
		            "      List Geolocation Profile Objects\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3 && a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 5) {
		if (strcasecmp(a->argv[3], "like")) {
			return CLI_SHOWUSAGE;
		}
		using_regex = 1;
	}

	sorted_container = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sorcery_object_id_sort, NULL);
	if (!sorted_container) {
		ast_cli(a->fd, "Geolocation Profile Objects: Unable to allocate temporary container\n");
		return CLI_FAILURE;
	}

	/* Get a sorted snapshot of the scheduled tasks */
	if (using_regex) {
		unsorted_container = ast_sorcery_retrieve_by_regex(geoloc_sorcery, "profile", a->argv[4]);
	} else {
		unsorted_container = ast_sorcery_retrieve_by_fields(geoloc_sorcery, "profile",
			AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	}

	ret = ao2_container_dup(sorted_container, unsorted_container, 0);
	ao2_ref(unsorted_container, -1);
	if (ret != 0) {
		ao2_ref(sorted_container, -1);
		ast_cli(a->fd, "Geolocation Profile Objects: Unable to sort temporary container\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Geolocation Profile Objects:\n\n");

	ast_cli(a->fd,
		"<Object ID...................................> <Profile Action> <Location Reference> \n"
		"=====================================================================================\n");

	iter = ao2_iterator_init(sorted_container, AO2_ITERATOR_UNLINK);
	for (; (profile = ao2_iterator_next(&iter)); ao2_ref(profile, -1)) {
		ao2_lock(profile);

		ast_cli(a->fd, "%-46.46s %-16s %-s\n",
			ast_sorcery_object_get_id(profile),
			precedence_names[profile->precedence],
			profile->location_reference);
		ao2_unlock(profile);
		count++;
	}
	ao2_iterator_destroy(&iter);
	ao2_ref(sorted_container, -1);
	ast_cli(a->fd, "\nTotal Profile Objects: %d\n\n", count);

	return result;
}

static char *geoloc_config_show_profiles(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator iter;
	struct ao2_container *sorted_container;
	struct ao2_container *unsorted_container;
	struct ast_geoloc_profile *profile;
	int using_regex = 0;
	char *result = CLI_SUCCESS;
	int ret = 0;
	int count = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "geoloc show profiles";
		e->usage = "Usage: geoloc show profiles [ like <pattern> ]\n"
		            "      List Geolocation Profile Objects\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3 && a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 5) {
		if (strcasecmp(a->argv[3], "like")) {
			return CLI_SHOWUSAGE;
		}
		using_regex = 1;
	}

	/* Create an empty rb-tree container which always sorts its contents. */
	sorted_container = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sorcery_object_id_sort, NULL);
	if (!sorted_container) {
		ast_cli(a->fd, "Geolocation Profile Objects: Unable to allocate temporary container\n");
		return CLI_FAILURE;
	}

	/* Get an unsorted list of profile parameters */
	if (using_regex) {
		unsorted_container = ast_sorcery_retrieve_by_regex(geoloc_sorcery, "profile", a->argv[4]);
	} else {
		unsorted_container = ast_sorcery_retrieve_by_fields(geoloc_sorcery, "profile",
			AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	}

	/* Copy the unsorted parameters into the rb-tree container which will sort them automatically. */
	ret = ao2_container_dup(sorted_container, unsorted_container, 0);
	ao2_ref(unsorted_container, -1);
	if (ret != 0) {
		ao2_ref(sorted_container, -1);
		ast_cli(a->fd, "Geolocation Profile Objects: Unable to sort temporary container\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Geolocation Profile Objects:\n");

	iter = ao2_iterator_init(sorted_container, AO2_ITERATOR_UNLINK);
	for (; (profile = ao2_iterator_next(&iter)); ) {
		struct ast_str *loc_str = NULL;
		struct ast_str *refinement_str = NULL;
		struct ast_str *variables_str = NULL;
		struct ast_str *resolved_str = NULL;
		struct ast_str *usage_rules_str = NULL;
		struct ast_str *confidence_str = NULL;
		struct ast_geoloc_eprofile *eprofile = ast_geoloc_eprofile_create_from_profile(profile);
		ao2_ref(profile, -1);

		loc_str = ast_variable_list_join(eprofile->location_info, ",", "=", "\"", NULL);
		resolved_str = ast_variable_list_join(eprofile->effective_location, ",", "=", "\"", NULL);

		refinement_str = ast_variable_list_join(eprofile->location_refinement, ",", "=", "\"", NULL);
		variables_str = ast_variable_list_join(eprofile->location_variables, ",", "=", "\"", NULL);
		usage_rules_str = ast_variable_list_join(eprofile->usage_rules, ",", "=", "\"", NULL);
		confidence_str = ast_variable_list_join(eprofile->confidence, ",", "=", "\"", NULL);

		ast_cli(a->fd,"\n"
			"id:                      %-s\n"
			"profile_precedence:      %-s\n"
			"pidf_element:            %-s\n"
			"location_reference:      %-s\n"
			"location_format:         %-s\n"
			"location_info:           %-s\n"
			"location_method:         %-s\n"
			"location_source:         %-s\n"
			"location_confidence:     %-s\n"
			"location_refinement:     %-s\n"
			"location_variables:      %-s\n"
			"allow_routing_use:       %-s\n"
			"suppress_empty_elements: %-s\n"
			"effective_location:      %-s\n"
			"usage_rules:             %-s\n"
			"notes:                   %-s\n",
			eprofile->id,
			precedence_names[eprofile->precedence],
			pidf_element_names[eprofile->pidf_element],
			S_OR(eprofile->location_reference, "<none>"),
			format_names[eprofile->format],
			S_COR(loc_str, ast_str_buffer(loc_str), "<none>"),
			S_OR(eprofile->method, "<none>"),
			S_OR(eprofile->location_source, "<none>"),
			S_COR(confidence_str, ast_str_buffer(confidence_str), "<none>"),
			S_COR(refinement_str, ast_str_buffer(refinement_str), "<none>"),
			S_COR(variables_str, ast_str_buffer(variables_str), "<none>"),
			S_COR(eprofile->allow_routing_use, "yes", "no"),
			S_COR(eprofile->suppress_empty_ca_elements, "yes", "no"),
			S_COR(resolved_str, ast_str_buffer(resolved_str), "<none>"),
			S_COR(usage_rules_str, ast_str_buffer(usage_rules_str), "<none>"),
			S_OR(eprofile->notes, "<none>")
			);
		ao2_ref(eprofile, -1);

		ast_free(loc_str);
		ast_free(refinement_str);
		ast_free(variables_str);
		ast_free(resolved_str);
		ast_free(usage_rules_str);
		ast_free(confidence_str);
		count++;
	}
	ao2_iterator_destroy(&iter);
	ao2_ref(sorted_container, -1);
	ast_cli(a->fd, "\nTotal Profile Objects: %d\n\n", count);

	return result;
}

static char *geoloc_config_cli_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *result = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "geoloc reload";
		e->usage = "Usage: geoloc reload\n"
		            "      Reload Geolocation Configuration\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2) {
		return CLI_SHOWUSAGE;
	}

	geoloc_config_reload();
	ast_cli(a->fd, "Geolocation Configuration reloaded.\n");

	return result;
}

static struct ast_cli_entry geoloc_location_cli_commands[] = {
	AST_CLI_DEFINE(geoloc_config_list_locations, "List Geolocation Location Objects"),
	AST_CLI_DEFINE(geoloc_config_list_profiles, "List Geolocation Profile Objects"),
	AST_CLI_DEFINE(geoloc_config_show_profiles, "Show Geolocation Profile Objects"),
	AST_CLI_DEFINE(geoloc_config_cli_reload, "Reload Geolocation Configuration"),
};

struct ast_geoloc_location * AST_OPTIONAL_API_NAME(ast_geoloc_get_location)(const char *id)
{
	if (ast_strlen_zero(id)) {
		return NULL;
	}

	return ast_sorcery_retrieve_by_id(geoloc_sorcery, "location", id);
}

struct ast_geoloc_profile * AST_OPTIONAL_API_NAME(ast_geoloc_get_profile)(const char *id)
{
	if (ast_strlen_zero(id)) {
		return NULL;
	}

	return ast_sorcery_retrieve_by_id(geoloc_sorcery, "profile", id);
}

int geoloc_config_reload(void)
{
	if (geoloc_sorcery) {
		ast_sorcery_reload(geoloc_sorcery);
	}
	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_config_unload(void)
{
	ast_cli_unregister_multiple(geoloc_location_cli_commands, ARRAY_LEN(geoloc_location_cli_commands));

	ast_sorcery_object_unregister(geoloc_sorcery, "profile");
	ast_sorcery_object_unregister(geoloc_sorcery, "location");

	if (geoloc_sorcery) {
		ast_sorcery_unref(geoloc_sorcery);
	}
	geoloc_sorcery = NULL;

	return 0;
}

static int default_profile_create(const char *name)
{
	int rc = 0;
	struct ast_geoloc_profile *profile;
	char *id = ast_alloca(strlen(name) + 3 /* <, >, NULL */);

	sprintf(id, "<%s>", name); /* Safe */
	profile = ast_sorcery_alloc(geoloc_sorcery, "profile", id);
	ast_assert_return(profile != NULL, 0);

	profile->precedence = ast_geoloc_precedence_str_to_enum(name);
	profile->pidf_element = AST_PIDF_ELEMENT_DEVICE;
	rc = ast_sorcery_create(geoloc_sorcery, profile);
	/*
	 * We're either passing the ref to sorcery or there was an error.
	 * Either way we need to drop our reference.
	 */
	ao2_ref(profile, -1);

	/* ast_assert_return wants a true/false */
	return rc == 0 ? 1 : 0;
}

static int geoloc_load_default_profiles(void)
{
	/*
	 * If any of these fail, the module will fail to load
	 * and clean up the sorcery instance so no error cleanup
	 * is required here.
	 */
	ast_assert_return(default_profile_create("prefer_config"), -1);
	ast_assert_return(default_profile_create("discard_config"), -1);
	ast_assert_return(default_profile_create("prefer_incoming"), -1);
	ast_assert_return(default_profile_create("discard_incoming"), -1);

	return 0;
}

int geoloc_config_load(void)
{
	enum ast_sorcery_apply_result result;
	int rc = 0;

	if (!(geoloc_sorcery = ast_sorcery_open())) {
		ast_log(LOG_ERROR, "Failed to open geolocation sorcery\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_apply_config(geoloc_sorcery, "location");
	result = ast_sorcery_apply_default(geoloc_sorcery, "location", "config", "geolocation.conf,criteria=type=location");
	if (result != AST_SORCERY_APPLY_SUCCESS) {
		ast_log(LOG_ERROR, "Failed to apply defaults for geoloc location object with sorcery\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	rc = ast_sorcery_object_register(geoloc_sorcery, "location", geoloc_location_alloc, NULL, geoloc_location_apply_handler);
	if (rc != 0) {
		ast_log(LOG_ERROR, "Failed to register geoloc location object with sorcery\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(geoloc_sorcery, "location", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "location", "format", AST_GEOLOC_FORMAT_NONE,
		location_format_handler, location_format_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "location", "location_info", NULL,
		location_location_info_handler, location_location_info_to_str, location_location_info_dup, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "location", "confidence", NULL,
		location_confidence_handler, location_confidence_to_str, location_confidence_dup, 0, 0);
	ast_sorcery_object_field_register(geoloc_sorcery, "location", "location_source", "", OPT_STRINGFIELD_T,
		0, STRFLDSET(struct ast_geoloc_location, location_source));
	ast_sorcery_object_field_register(geoloc_sorcery, "location", "method", "", OPT_STRINGFIELD_T,
		0, STRFLDSET(struct ast_geoloc_location, method));


	ast_sorcery_apply_config(geoloc_sorcery, "profile");
	/*
	 * The memory backend is used to contain the built-in profiles.
	 */
	result = ast_sorcery_apply_wizard_mapping(geoloc_sorcery, "profile", "memory", NULL, 0);
	if (result == AST_SORCERY_APPLY_FAIL) {
		ast_log(LOG_ERROR, "Failed to add memory wizard mapping to geoloc profile object\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	result = ast_sorcery_apply_wizard_mapping(geoloc_sorcery, "profile", "config",
		"geolocation.conf,criteria=type=profile", 0);
	if (result == AST_SORCERY_APPLY_FAIL) {
		ast_log(LOG_ERROR, "Failed to add memory wizard mapping to geoloc profile object\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	rc = ast_sorcery_object_register(geoloc_sorcery, "profile", geoloc_profile_alloc, NULL, geoloc_profile_apply_handler);
	if (rc != 0) {
		ast_log(LOG_ERROR, "Failed to register geoloc profile object with sorcery\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(geoloc_sorcery, "profile", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "profile", "pidf_element",
		pidf_element_names[AST_PIDF_ELEMENT_DEVICE], profile_pidf_element_handler, profile_pidf_element_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(geoloc_sorcery, "profile", "location_reference", "", OPT_STRINGFIELD_T,
		0, STRFLDSET(struct ast_geoloc_profile, location_reference));
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "profile", "profile_precedence", "discard_incoming",
		profile_precedence_handler, profile_precedence_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "profile", "usage_rules", NULL,
		profile_usage_rules_handler, profile_usage_rules_to_str, profile_usage_rules_dup, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "profile", "location_info_refinement", NULL,
		profile_location_refinement_handler, profile_location_refinement_to_str, profile_location_refinement_dup, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "profile", "location_variables", NULL,
		profile_location_variables_handler, profile_location_variables_to_str, profile_location_variables_dup, 0, 0);
	ast_sorcery_object_field_register(geoloc_sorcery, "profile", "notes", "", OPT_STRINGFIELD_T,
		0, STRFLDSET(struct ast_geoloc_profile, notes));
	ast_sorcery_object_field_register(geoloc_sorcery, "profile", "allow_routing_use",
		"no", OPT_BOOL_T, 1, FLDSET(struct ast_geoloc_profile, allow_routing_use));
	ast_sorcery_object_field_register(geoloc_sorcery, "profile", "suppress_empty_ca_elements",
		"no", OPT_BOOL_T, 1, FLDSET(struct ast_geoloc_profile, suppress_empty_ca_elements));

	ast_sorcery_object_field_register_custom(geoloc_sorcery, "profile", "format", AST_GEOLOC_FORMAT_NONE,
		profile_format_handler, profile_format_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "profile", "location_info", NULL,
		profile_location_info_handler, profile_location_info_to_str, profile_location_info_dup, 0, 0);
	ast_sorcery_object_field_register_custom(geoloc_sorcery, "profile", "confidence", NULL,
		profile_confidence_handler, profile_confidence_to_str, profile_confidence_dup, 0, 0);
	ast_sorcery_object_field_register(geoloc_sorcery, "profile", "location_source", "", OPT_STRINGFIELD_T,
		0, STRFLDSET(struct ast_geoloc_profile, location_source));
	ast_sorcery_object_field_register(geoloc_sorcery, "profile", "method", "", OPT_STRINGFIELD_T,
		0, STRFLDSET(struct ast_geoloc_profile, method));


	ast_sorcery_load(geoloc_sorcery);

	rc = geoloc_load_default_profiles();
	if (rc != 0) {
		ast_log(LOG_ERROR, "Failed to load default geoloc profiles\n");
		return AST_MODULE_LOAD_DECLINE;
	}


	ast_cli_register_multiple(geoloc_location_cli_commands, ARRAY_LEN(geoloc_location_cli_commands));

	return AST_MODULE_LOAD_SUCCESS;
}

int AST_OPTIONAL_API_NAME(ast_geoloc_is_loaded)(void)
{
	return 1;
}

