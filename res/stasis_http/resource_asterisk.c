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
 * \brief Implementation for stasis-http stubs.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "resource_asterisk.h"
#include "asterisk/pbx.h"

void stasis_http_get_asterisk_info(struct ast_variable *headers, struct ast_get_asterisk_info_args *args, struct stasis_http_response *response)
{
	ast_log(LOG_ERROR, "TODO: stasis_http_get_asterisk_info\n");
}

void stasis_http_get_global_var(struct ast_variable *headers, struct ast_get_global_var_args *args, struct stasis_http_response *response)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	RAII_VAR(struct ast_str *, tmp, ast_str_create(32), ast_free);

	const char *value;

	ast_assert(response != NULL);

	if (!tmp) {
		stasis_http_response_alloc_failed(response);
		return;
	}

	value = ast_str_retrieve_variable(&tmp, 0, NULL, NULL, args->variable);

	if (!(json = ast_json_pack("{s: s}", "value", S_OR(value, "")))) {
		stasis_http_response_alloc_failed(response);
		return;
	}

	stasis_http_response_ok(response, ast_json_ref(json));
}

void stasis_http_set_global_var(struct ast_variable *headers, struct ast_set_global_var_args *args, struct stasis_http_response *response)
{
	ast_assert(response != NULL);

	if (ast_strlen_zero(args->variable)) {
		stasis_http_response_error(
			response, 400, "Bad Request",
			"Variable name is required");
		return;
	}

	pbx_builtin_setvar_helper(NULL, args->variable, args->value);

	stasis_http_response_no_content(response);
}
