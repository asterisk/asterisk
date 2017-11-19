/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief Get information about a PJSIP endpoint
 *
 * \author \verbatim Matt Jordan <mjordan@digium.com> \endverbatim
 *
 * \ingroup functions
 *
 */

/*** MODULEINFO
	<support_level>core</support_level>
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjlib.h>

#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/sorcery.h"
#include "asterisk/res_pjsip.h"

/*** DOCUMENTATION
	<function name="PJSIP_ENDPOINT" language="en_US">
		<synopsis>
			Get information about a PJSIP endpoint
		</synopsis>
		<syntax>
			<parameter name="name" required="true">
				<para>The name of the endpoint to query.</para>
			</parameter>
			<parameter name="field" required="true">
				<para>The configuration option for the endpoint to query for.
				Supported options are those fields on the
				<replaceable>endpoint</replaceable> object in
				<filename>pjsip.conf</filename>.</para>
				<enumlist>
					<configOptionToEnum>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption)"/>
					</configOptionToEnum>
				</enumlist>
			</parameter>
		</syntax>
	</function>
***/

static int pjsip_endpoint_function_read(struct ast_channel *chan,
	const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	struct ast_sorcery *pjsip_sorcery;
	char *parsed_data = ast_strdupa(data);
	RAII_VAR(void *, endpoint_obj, NULL, ao2_cleanup);
	struct ast_variable *change_set;
	struct ast_variable *it_change_set;
	int res;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(endpoint_name);
		AST_APP_ARG(field_name);
	);

	/* Check for zero arguments */
	if (ast_strlen_zero(parsed_data)) {
		ast_log(AST_LOG_ERROR, "Cannot call %s without arguments\n", cmd);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parsed_data);

	if (ast_strlen_zero(args.endpoint_name)) {
		ast_log(AST_LOG_ERROR, "Cannot call %s without an endpoint name to query\n", cmd);
		return -1;
	}

	if (ast_strlen_zero(args.field_name)) {
		ast_log(AST_LOG_ERROR, "Cannot call %s with an empty field name to query\n", cmd);
		return -1;
	}

	pjsip_sorcery = ast_sip_get_sorcery();
	if (!pjsip_sorcery) {
		ast_log(AST_LOG_ERROR, "Unable to retrieve PJSIP configuration: sorcery object is NULL\n");
		return -1;
	}

	endpoint_obj = ast_sorcery_retrieve_by_id(pjsip_sorcery, "endpoint", args.endpoint_name);
	if (!endpoint_obj) {
		ast_log(AST_LOG_WARNING, "Failed to retrieve information for endpoint '%s'\n", args.endpoint_name);
		return -1;
	}

	change_set = ast_sorcery_objectset_create(pjsip_sorcery, endpoint_obj);
	if (!change_set) {
		ast_log(AST_LOG_WARNING, "Failed to retrieve information for endpoint '%s': change set is NULL\n", args.endpoint_name);
		return -1;
	}

	for (it_change_set = change_set; it_change_set; it_change_set = it_change_set->next) {
		if (!strcmp(it_change_set->name, args.field_name)) {
			if (!strcmp(it_change_set->name, "disallow")) {
				ast_str_set(buf, len, "!%s", it_change_set->value);
			} else {
				ast_str_set(buf, len, "%s", it_change_set->value);
			}
			break;
		}
	}

	res = it_change_set ? 0 : 1;
	if (res) {
		ast_log(AST_LOG_WARNING, "Unknown property '%s' for PJSIP endpoint\n", args.field_name);
	}

	ast_variables_destroy(change_set);

	return res;
}


static struct ast_custom_function pjsip_endpoint_function = {
	.name = "PJSIP_ENDPOINT",
	.read2 = pjsip_endpoint_function_read,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&pjsip_endpoint_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&pjsip_endpoint_function);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Get information about a PJSIP endpoint",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_pjsip",
);
