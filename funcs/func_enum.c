/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006
 *
 * Mark Spencer <markster@digium.com>
 * Oleksiy Krivoshey <oleksiyk@gmail.com>
 * Russell Bryant <russelb@clemson.edu>
 * Brett Bryant <bbryant@digium.com>
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
 * \brief ENUM Functions
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Oleksiy Krivoshey <oleksiyk@gmail.com>
 * \author Russell Bryant <russelb@clemson.edu>
 * \author Brett Bryant <bbryant@digium.com>
 *
 * \arg See also AstENUM
 *
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/enum.h"
#include "asterisk/app.h"

static char *synopsis = "Syntax: ENUMLOOKUP(number[,Method-type[,options[,record#[,zone-suffix]]]])\n";

static int function_enum(struct ast_channel *chan, const char *cmd, char *data,
			 char *buf, size_t len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(number);
		AST_APP_ARG(tech);
		AST_APP_ARG(options);
		AST_APP_ARG(record);
		AST_APP_ARG(zone);
	);
	int res = 0;
	char tech[80];
	char dest[256] = "", tmp[2] = "", num[AST_MAX_EXTENSION] = "";
	char *s, *p;
	unsigned int record = 1;

	buf[0] = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, synopsis);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc < 1) {
		ast_log(LOG_WARNING, synopsis);
		return -1;
	}

	ast_copy_string(tech, args.tech ? args.tech : "sip", sizeof(tech));

	if (!args.zone)
		args.zone = "e164.arpa";

	if (!args.options)
		args.options = "";

	if (args.record)
		record = atoi(args.record);

	/* strip any '-' signs from number */
	for (s = p = args.number; *s; s++) {
		if (*s != '-') {
			snprintf(tmp, sizeof(tmp), "%c", *s);
			strncat(num, tmp, sizeof(num));
		}

	}

	res = ast_get_enum(chan, num, dest, sizeof(dest), tech, sizeof(tech), args.zone, args.options, 1, NULL);

	p = strchr(dest, ':');
	if (p && strcasecmp(tech, "ALL"))
		ast_copy_string(buf, p + 1, len);
	else
		ast_copy_string(buf, dest, len);

	return 0;
}

unsigned int enum_datastore_id;

struct enum_result_datastore {
	struct enum_context *context;
	unsigned int id;
};

static void erds_destroy(struct enum_result_datastore *data) 
{
	int k;

	for (k = 0; k < data->context->naptr_rrs_count; k++) {
		ast_free(data->context->naptr_rrs[k].result);
		ast_free(data->context->naptr_rrs[k].tech);
	}

	ast_free(data->context->naptr_rrs);
	ast_free(data->context);
	ast_free(data);
}

static void erds_destroy_cb(void *data) 
{
	struct enum_result_datastore *erds = data;
	erds_destroy(erds);
}

const struct ast_datastore_info enum_result_datastore_info = {
	.type = "ENUMQUERY",
	.destroy = erds_destroy_cb,
}; 

static int enum_query_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct enum_result_datastore *erds;
	struct ast_datastore *datastore;
	char *parse, tech[128], dest[128];
	int res = -1;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(number);
		AST_APP_ARG(tech);
		AST_APP_ARG(zone);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ENUMQUERY requires at least a number as an argument...\n");
		goto finish;
	}

	parse = ast_strdupa(data);
    
	AST_STANDARD_APP_ARGS(args, parse);

	if (!chan) {
		ast_log(LOG_ERROR, "ENUMQUERY cannot be used without a channel!\n");
		goto finish;
	}

	if (!args.zone)
		args.zone = "e164.zone";

	ast_copy_string(tech, args.tech ? args.tech : "sip", sizeof(tech));

	if (!(erds = ast_calloc(1, sizeof(*erds))))
		goto finish;

	if (!(erds->context = ast_calloc(1, sizeof(*erds->context)))) {
		ast_free(erds);
		goto finish;
	}

	erds->id = ast_atomic_fetchadd_int((int *) &enum_datastore_id, 1);

	snprintf(buf, len, "%u", erds->id);

	if (!(datastore = ast_channel_datastore_alloc(&enum_result_datastore_info, buf))) {
		ast_free(erds->context);
		ast_free(erds);
		goto finish;
	}

	ast_get_enum(chan, args.number, dest, sizeof(dest), tech, sizeof(tech), args.zone, "", 1, &erds->context);

	datastore->data = erds;

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);
   
	res = 0;
    
finish:

	return res;
}

static int enum_result_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct enum_result_datastore *erds;
	struct ast_datastore *datastore;
	char *parse, *p;
	unsigned int num;
	int res = -1, k;
	AST_DECLARE_APP_ARGS(args, 
		AST_APP_ARG(id);
		AST_APP_ARG(resultnum);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ENUMRESULT requires two arguments (id and resultnum)\n");
		goto finish;
	}

	if (!chan) {
		ast_log(LOG_ERROR, "ENUMRESULT can not be used without a channel!\n");
		goto finish;
	}
   
	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.id)) {
		ast_log(LOG_ERROR, "A result ID must be provided to ENUMRESULT\n");
		goto finish;
	}

	if (ast_strlen_zero(args.resultnum)) {
		ast_log(LOG_ERROR, "A result number must be given to ENUMRESULT!\n");
		goto finish;
	}

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &enum_result_datastore_info, args.id);
	ast_channel_unlock(chan);
	if (!datastore) {
		ast_log(LOG_WARNING, "No ENUM results found for query id!\n");
		goto finish;
	}

	erds = datastore->data;

	if (!strcasecmp(args.resultnum, "getnum")) {
		snprintf(buf, len, "%u", erds->context->naptr_rrs_count);
		res = 0;
		goto finish;
	}

	if (sscanf(args.resultnum, "%u", &num) != 1) {
		ast_log(LOG_ERROR, "Invalid value '%s' for resultnum to ENUMRESULT!\n", args.resultnum);
		goto finish;
	}

	if (!num || num > erds->context->naptr_rrs_count) {
		ast_log(LOG_WARNING, "Result number %u is not valid for ENUM query results for ID %s!\n", num, args.id);
		goto finish;
	}

	for (k = 0; k < erds->context->naptr_rrs_count; k++) {
		if (num - 1 != erds->context->naptr_rrs[k].sort_pos)
			continue;

		p = strchr(erds->context->naptr_rrs[k].result, ':');
              
		if (p && strcasecmp(erds->context->naptr_rrs[k].tech, "ALL"))
			ast_copy_string(buf, p + 1, len);
		else
			ast_copy_string(buf, erds->context->naptr_rrs[k].result, len);

		break;
	}

	res = 0;

finish:

	return res;
}

static struct ast_custom_function enum_query_function = {
	.name = "ENUMQUERY",
	.synopsis = "Initiate an ENUM query",
	.syntax = "ENUMQUERY(number[,Method-type[,zone-suffix]])",
	.desc = "This will do a ENUM lookup of the given phone number.\n"
	"If no method-tpye is given, the default will be sip. If no\n"
	"zone-suffix is given, the default will be \"e164.arpa\".\n"
	"The result of this function will be a numeric ID that can\n"
	"be used to retrieve the results using the ENUMRESULT function.\n",
	.read = enum_query_read,
};

static struct ast_custom_function enum_result_function = {
	.name = "ENUMRESULT",
	.synopsis = "Retrieve results from a ENUMQUERY",
	.syntax = "ENUMRESULT(id,resultnum)",
	.desc = "This function will retrieve results from a previous use\n"
	"of the ENUMQUERY function.\n"
	"  id - This argument is the identifier returned by the ENUMQUERY function.\n"
	"  resultnum - This is the number of the result that you want to retrieve.\n"
	"       Results start at 1.  If this argument is specified as \"getnum\",\n"
	"       then it will return the total number of results that are available.\n",
	.read = enum_result_read,
};

static struct ast_custom_function enum_function = {
	.name = "ENUMLOOKUP",
	.synopsis =
		"General or specific querying of NAPTR records for ENUM or ENUM-like DNS pointers",
	.syntax =
		"ENUMLOOKUP(number[,Method-type[,options[,record#[,zone-suffix]]]])",
	.desc =
		"Option 'c' returns an integer count of the number of NAPTRs of a certain RR type.\n"
		"Combination of 'c' and Method-type of 'ALL' will return a count of all NAPTRs for the record.\n"
		"Defaults are: Method-type=sip, no options, record=1, zone-suffix=e164.arpa\n\n"
		"For more information, see doc/asterisk.pdf",
	.read = function_enum,
};

static int function_txtcidname(struct ast_channel *chan, const char *cmd,
			       char *data, char *buf, size_t len)
{
	int res;
	char tech[80];
	char txt[256] = "";
	char dest[80];

	buf[0] = '\0';


	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "TXTCIDNAME requires an argument (number)\n");
		return -1;
	}

	res = ast_get_txt(chan, data, dest, sizeof(dest), tech, sizeof(tech), txt,
			  sizeof(txt));

	if (!ast_strlen_zero(txt))
		ast_copy_string(buf, txt, len);

	return 0;
}

static struct ast_custom_function txtcidname_function = {
	.name = "TXTCIDNAME",
	.synopsis = "TXTCIDNAME looks up a caller name via DNS",
	.syntax = "TXTCIDNAME(<number>)",
	.desc =
		"This function looks up the given phone number in DNS to retrieve\n"
		"the caller id name.  The result will either be blank or be the value\n"
		"found in the TXT record in DNS.\n",
	.read = function_txtcidname,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&enum_result_function);
	res |= ast_custom_function_unregister(&enum_query_function);
	res |= ast_custom_function_unregister(&enum_function);
	res |= ast_custom_function_unregister(&txtcidname_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&enum_result_function);
	res |= ast_custom_function_register(&enum_query_function);
	res |= ast_custom_function_register(&enum_function);
	res |= ast_custom_function_register(&txtcidname_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "ENUM related dialplan functions");
