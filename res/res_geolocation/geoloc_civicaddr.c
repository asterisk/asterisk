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
#include "asterisk/res_geolocation.h"
#include "asterisk/xml.h"
#include "geoloc_private.h"

static const char *addr_code_name_entries[] = {
	"country",
	"A1",
	"A2",
	"A3",
	"A4",
	"A5",
	"A6",
	"ADDCODE",
	"BLD",
	"FLR",
	"HNO",
	"HNS",
	"LMK",
	"LOC",
	"NAM",
	"PC",
	"PCN",
	"PLC",
	"POBOX",
	"POD",
	"POM",
	"PRD",
	"PRM",
	"RD",
	"RD",
	"RDBR",
	"RDSEC",
	"RDSUBBR",
	"ROOM",
	"SEAT",
	"STS",
	"UNIT",
};

static int compare_civicaddr_codes(const void *_a, const void *_b)
{
	/* See the man page for qsort(3) for an explanation of the casts */
	int rc = strcmp(*(const char **)_a, *(const char **)_b);
	return rc;
}

int ast_geoloc_civicaddr_is_code_valid(const char *code)
{
	const char **entry = bsearch(&code, addr_code_name_entries, ARRAY_LEN(addr_code_name_entries),
		sizeof(const char *), compare_civicaddr_codes);
	return (entry != NULL);
}

enum ast_geoloc_validate_result ast_geoloc_civicaddr_validate_varlist(
	const struct ast_variable *varlist,	const char **result)
{
	const struct ast_variable *var = varlist;
	for (; var; var = var->next) {
		int valid = ast_geoloc_civicaddr_is_code_valid(var->name);
		if (!valid) {
			*result = var->name;
			return AST_GEOLOC_VALIDATE_INVALID_VARNAME;
		}
	}
	return AST_GEOLOC_VALIDATE_SUCCESS;
}

struct ast_xml_node *geoloc_civicaddr_list_to_xml(const struct ast_variable *resolved_location,
	const char *ref_string)
{
	char *lang = NULL;
	char *s = NULL;
	struct ast_variable *var;
	struct ast_xml_node *ca_node;
	struct ast_xml_node *child_node;
	int rc = 0;
	SCOPE_ENTER(3, "%s", ref_string);

	lang = (char *)ast_variable_find_in_list(resolved_location, "lang");
	if (ast_strlen_zero(lang)) {
		lang = ast_strdupa(ast_defaultlanguage);
		for (s = lang; *s; s++) {
			if (*s == '_') {
				*s = '-';
			}
		}
	}

	ca_node = ast_xml_new_node("civicAddress");
	if (!ca_node) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create 'civicAddress' XML node\n", ref_string);
	}
	rc = ast_xml_set_attribute(ca_node, "lang", lang);
	if (rc != 0) {
		ast_xml_free_node(ca_node);
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create 'lang' XML attribute\n", ref_string);
	}

	for (var = (struct ast_variable *)resolved_location; var; var = var->next) {
		if (ast_strings_equal(var->name, "lang")) {
			continue;
		}
		child_node = ast_xml_new_child(ca_node, var->name);
		if (!child_node) {
			ast_xml_free_node(ca_node);
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create '%s' XML node\n", var->name, ref_string);
		}
		ast_xml_set_text(child_node, var->value);
	}

	SCOPE_EXIT_RTN_VALUE(ca_node, "%s: Done\n", ref_string);
}

int geoloc_civicaddr_unload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_civicaddr_load(void)
{
	qsort(addr_code_name_entries, ARRAY_LEN(addr_code_name_entries), sizeof(const char *),
		compare_civicaddr_codes);

	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_civicaddr_reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}
