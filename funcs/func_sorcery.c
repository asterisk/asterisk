/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Fairview 5 Engineering, LLC
 *
 * George Joseph <george.joseph@fairview5.com>
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
 * \brief Get a field from a sorcery object
 *
 * \author \verbatim George Joseph <george.joseph@fairview5.com> \endverbatim
 *
 * \ingroup functions
 *
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/sorcery.h"

/*** DOCUMENTATION
	<function name="AST_SORCERY" language="en_US">
		<synopsis>
			Get a field from a sorcery object
		</synopsis>
		<syntax>
			<parameter name="module_name" required="true">
				<para>The name of the module owning the sorcery instance.</para>
			</parameter>
			<parameter name="object_type" required="true">
				<para>The type of object to query.</para>
			</parameter>
			<parameter name="object_id" required="true">
				<para>The id of the object to query.</para>
			</parameter>
			<parameter name="field_name" required="true">
				<para>The name of the field.</para>
			</parameter>
			<parameter name="retrieval_method" required="false">
				<para>Fields that have multiple occurrences may be retrieved in two ways.</para>
				<enumlist>
					<enum name="concat"><para>Returns all matching fields concatenated
					in a single string separated by <replaceable>separator</replaceable>
					which defaults to <literal>,</literal>.</para></enum>

					<enum name="single"><para>Returns the nth occurrence of the field
					as specified by <replaceable>occurrence_number</replaceable> which defaults to <literal>1</literal>.
					</para></enum>
				</enumlist>
				<para>The default is <literal>concat</literal> with separator <literal>,</literal>.</para>
			</parameter>
			<parameter name="retrieval_details" required="false">
				<para>Specifies either the separator for <literal>concat</literal>
				or the occurrence number for <literal>single</literal>.</para>
			</parameter>
		</syntax>
	</function>
***/

static int sorcery_function_read(struct ast_channel *chan,
	const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	char *parsed_data = ast_strdupa(data);
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(void *, sorcery_obj, NULL, ao2_cleanup);
	struct ast_variable *change_set;
	struct ast_variable *it_change_set;
	int found, field_number = 1, ix, method;
	char *separator = ",";

	enum methods {
		CONCAT,
		SINGLE,
	};

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(module_name);
		AST_APP_ARG(object_type);
		AST_APP_ARG(object_id);
		AST_APP_ARG(field_name);
		AST_APP_ARG(method);
		AST_APP_ARG(method_arg);
	);

	/* Check for zero arguments */
	if (ast_strlen_zero(parsed_data)) {
		ast_log(AST_LOG_ERROR, "Cannot call %s without arguments\n", cmd);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parsed_data);

	if (ast_strlen_zero(args.module_name)) {
		ast_log(AST_LOG_ERROR, "Cannot call %s without a module name to query\n", cmd);
		return -1;
	}

	if (ast_strlen_zero(args.object_type)) {
		ast_log(AST_LOG_ERROR, "Cannot call %s with an empty object type\n", cmd);
		return -1;
	}

	if (ast_strlen_zero(args.object_id)) {
		ast_log(AST_LOG_ERROR, "Cannot call %s with an empty object name\n", cmd);
		return -1;
	}

	if (ast_strlen_zero(args.field_name)) {
		ast_log(AST_LOG_ERROR, "Cannot call %s with an empty field name\n", cmd);
		return -1;
	}

	if (ast_strlen_zero(args.method)) {
		method = CONCAT;
	} else {
		if (strcmp(args.method, "concat") == 0) {
			method = CONCAT;
			if (ast_strlen_zero(args.method_arg)) {
				separator = ",";
			} else {
				separator = args.method_arg;
			}

		} else if (strcmp(args.method, "single") == 0) {
			method = SINGLE;
			if (!ast_strlen_zero(args.method_arg)) {
				if (sscanf(args.method_arg, "%30d", &field_number) <= 0 || field_number <= 0 ) {
					ast_log(AST_LOG_ERROR, "occurrence_number must be a positive integer\n");
					return -1;
				}
			}
		} else {
			ast_log(AST_LOG_ERROR, "Retrieval method must be 'concat' or 'single'\n");
			return -1;
		}
	}

	sorcery = ast_sorcery_retrieve_by_module_name(args.module_name);
	if (!sorcery) {
		ast_log(AST_LOG_ERROR, "Failed to retrieve sorcery instance for module %s\n", args.module_name);
		return -1;
	}

	sorcery_obj = ast_sorcery_retrieve_by_id(sorcery, args.object_type, args.object_id);
	if (!sorcery_obj) {
		return -1;
	}

	change_set = ast_sorcery_objectset_create(sorcery, sorcery_obj);
	if (!change_set) {
		return -1;
	}

	ix=1;
	found = 0;
	for (it_change_set = change_set; it_change_set; it_change_set = it_change_set->next) {

		if (method == CONCAT && strcmp(it_change_set->name, args.field_name) == 0) {
			ast_str_append(buf, 0, "%s%s", it_change_set->value, separator);
			found = 1;
			continue;
		}

		if (method == SINGLE && strcmp(it_change_set->name, args.field_name) == 0  && ix++ == field_number) {
			ast_str_set(buf, len, "%s", it_change_set->value);
			found = 1;
			break;
		}
	}

	ast_variables_destroy(change_set);

	if (!found) {
		return -1;
	}

	if (method == CONCAT) {
		ast_str_truncate(*buf, -1);
	}

	return 0;
}

static struct ast_custom_function sorcery_function = {
	.name = "AST_SORCERY",
	.read2 = sorcery_function_read,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&sorcery_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&sorcery_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Get a field from a sorcery object");

