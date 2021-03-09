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
 * \brief Get information about a PJSIP contact
 *
 * \author \verbatim Joshua Colp <jcolp@digium.com> \endverbatim
 *
 * \ingroup functions
 *
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjlib.h>

#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/res_pjsip.h"

/*** DOCUMENTATION
	<function name="PJSIP_CONTACT" language="en_US">
		<synopsis>
			Get information about a PJSIP contact
		</synopsis>
		<syntax>
			<parameter name="name" required="true">
				<para>The name of the contact to query.</para>
			</parameter>
			<parameter name="field" required="true">
				<para>The configuration option for the contact to query for.
				Supported options are those fields on the
				<replaceable>contact</replaceable> object.</para>
				<enumlist>
					<configOptionToEnum>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='contact']/configOption)"/>
					</configOptionToEnum>
					<enum name="rtt">
						<para>The RTT of the last qualify</para>
					</enum>
					<enum name="status">
						<para>Status of the contact</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
	</function>
***/

static int contact_function_get_permanent(void *obj, void *arg, int flags)
{
	const char *id = arg;

	if (!strcmp(ast_sorcery_object_get_id(obj), id)) {
		return CMP_MATCH | CMP_STOP;
	}

	return 0;
}

static int pjsip_contact_function_read(struct ast_channel *chan,
	const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	struct ast_sorcery *pjsip_sorcery;
	char *parsed_data = ast_strdupa(data);
	char *contact_name;
	RAII_VAR(struct ast_sip_contact *, contact_obj, NULL, ao2_cleanup);
	RAII_VAR(struct ast_sip_contact_status *, contact_status, NULL, ao2_cleanup);
	int res = 0;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(contact_name);
		AST_APP_ARG(field_name);
	);

	/* Check for zero arguments */
	if (ast_strlen_zero(parsed_data)) {
		ast_log(AST_LOG_ERROR, "Cannot call %s without arguments\n", cmd);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parsed_data);

	if (ast_strlen_zero(args.contact_name)) {
		ast_log(AST_LOG_ERROR, "Cannot call %s without a contact name to query\n", cmd);
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

	/* Determine if this is a permanent contact or a normal contact */
	if ((contact_name = strstr(args.contact_name, "@@"))) {
		size_t aor_name_len = contact_name - args.contact_name;
		char aor_name[aor_name_len + 1];
		RAII_VAR(struct ast_sip_aor *, aor_obj, NULL, ao2_cleanup);

		/* Grab only the AOR name so we can retrieve the AOR which will give us the contact */
		strncpy(aor_name, args.contact_name, aor_name_len);
		aor_name[aor_name_len] = '\0';

		aor_obj = ast_sorcery_retrieve_by_id(pjsip_sorcery, "aor", aor_name);
		if (!aor_obj) {
			ast_log(AST_LOG_WARNING, "Failed to retrieve information for contact '%s'\n", args.contact_name);
			return -1;
		}

		contact_obj = ao2_callback(aor_obj->permanent_contacts, 0, contact_function_get_permanent, args.contact_name);
	} else {
		contact_obj = ast_sorcery_retrieve_by_id(pjsip_sorcery, "contact", args.contact_name);
	}

	if (!contact_obj) {
		ast_log(AST_LOG_WARNING, "Failed to retrieve information for contact '%s'\n", args.contact_name);
		return -1;
	}

	contact_status = ast_sip_get_contact_status(contact_obj);

	if (!strcmp(args.field_name, "status")) {
		ast_str_set(buf, len, "%s", ast_sip_get_contact_status_label(contact_status ? contact_status->status : UNKNOWN));
	} else if (!strcmp(args.field_name, "rtt")) {
		if (!contact_status || contact_status->status != AVAILABLE) {
			ast_str_set(buf, len, "%s", "N/A");
		} else {
			ast_str_set(buf, len, "%" PRId64, contact_status->rtt);
		}
	} else {
		struct ast_variable *change_set;
		struct ast_variable *it_change_set;

		change_set = ast_sorcery_objectset_create(pjsip_sorcery, contact_obj);

		if (!change_set) {
			ast_log(AST_LOG_WARNING, "Failed to retrieve information for contact '%s': change set is NULL\n", args.contact_name);
			return -1;
		}

		for (it_change_set = change_set; it_change_set; it_change_set = it_change_set->next) {
			if (!strcmp(it_change_set->name, args.field_name)) {
				ast_str_set(buf, len, "%s", it_change_set->value);
				break;
			}
		}

		if (!it_change_set) {
			ast_log(AST_LOG_WARNING, "Unknown property '%s' for PJSIP contact\n", args.field_name);

			res = 1;
		}

		ast_variables_destroy(change_set);
	}

	return res;
}


static struct ast_custom_function pjsip_contact_function = {
	.name = "PJSIP_CONTACT",
	.read2 = pjsip_contact_function_read,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&pjsip_contact_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&pjsip_contact_function);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Get information about a PJSIP contact",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_pjsip",
);
