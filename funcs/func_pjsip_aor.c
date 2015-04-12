/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Get information about a PJSIP AOR
 *
 * \author \verbatim Joshua Colp <jcolp@digium.com> \endverbatim
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

ASTERISK_REGISTER_FILE(__FILE__)

#include <pjsip.h>
#include <pjlib.h>

#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/res_pjsip.h"

/*** DOCUMENTATION
	<function name="PJSIP_AOR" language="en_US">
		<synopsis>
			Get information about a PJSIP AOR
		</synopsis>
		<syntax>
			<parameter name="name" required="true">
				<para>The name of the AOR to query.</para>
			</parameter>
			<parameter name="field" required="true">
				<para>The configuration option for the AOR to query for.
				Supported options are those fields on the
				<replaceable>aor</replaceable> object in
				<filename>pjsip.conf</filename>.</para>
				<enumlist>
					<configOptionToEnum>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption)"/>
					</configOptionToEnum>
				</enumlist>
			</parameter>
		</syntax>
	</function>
***/

static int pjsip_aor_function_read(struct ast_channel *chan,
	const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	struct ast_sorcery *pjsip_sorcery;
	char *parsed_data = ast_strdupa(data);
	RAII_VAR(struct ast_sip_aor *, aor_obj, NULL, ao2_cleanup);
	int res = 0;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(aor_name);
		AST_APP_ARG(field_name);
	);

	/* Check for zero arguments */
	if (ast_strlen_zero(parsed_data)) {
		ast_log(AST_LOG_ERROR, "Cannot call %s without arguments\n", cmd);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parsed_data);

	if (ast_strlen_zero(args.aor_name)) {
		ast_log(AST_LOG_ERROR, "Cannot call %s without an AOR name to query\n", cmd);
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

	aor_obj = ast_sorcery_retrieve_by_id(pjsip_sorcery, "aor", args.aor_name);
	if (!aor_obj) {
		ast_log(AST_LOG_WARNING, "Failed to retrieve information for AOR '%s'\n", args.aor_name);
		return -1;
	}

	if (!strcmp(args.field_name, "contact")) {
		/* The multiple fields handler for contact does not provide a list of contact object names, which is what we want, so we
		 * handle contact specifically to provide this.
		 */
		RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);
		struct ao2_iterator i;
		struct ast_sip_contact *contact;
		int first = 1;

		contacts = ast_sip_location_retrieve_aor_contacts(aor_obj);
		if (!contacts) {
			ast_log(LOG_WARNING, "Failed to retrieve contacts for AOR '%s'\n", args.aor_name);
			return -1;
		}

		i = ao2_iterator_init(contacts, 0);
		while ((contact = ao2_iterator_next(&i))) {
			if (!first) {
				ast_str_append(buf, len, "%s", ",");
			}

			ast_str_append(buf, len, "%s", ast_sorcery_object_get_id(contact));
			first = 0;
		}
		ao2_iterator_destroy(&i);
	} else {
		struct ast_variable *change_set;
		struct ast_variable *it_change_set;

		change_set = ast_sorcery_objectset_create(pjsip_sorcery, aor_obj);
		if (!change_set) {
			ast_log(AST_LOG_WARNING, "Failed to retrieve information for AOR '%s': change set is NULL\n", args.aor_name);
			return -1;
		}

		for (it_change_set = change_set; it_change_set; it_change_set = it_change_set->next) {
			if (!strcmp(it_change_set->name, args.field_name)) {
				ast_str_set(buf, len, "%s", it_change_set->value);
				break;
			}
		}

		if (!it_change_set) {
			ast_log(AST_LOG_WARNING, "Unknown property '%s' for PJSIP AOR\n", args.field_name);
			res = 1;
		}

		ast_variables_destroy(change_set);
	}

	return res;
}


static struct ast_custom_function pjsip_aor_function = {
	.name = "PJSIP_AOR",
	.read2 = pjsip_aor_function_read,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&pjsip_aor_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&pjsip_aor_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Get information about a PJSIP AOR");
