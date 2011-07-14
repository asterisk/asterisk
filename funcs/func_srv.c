/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010 Digium, Inc.
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
 * \brief SRV Functions
 *
 * \author Mark Michelson <mmichelson@digium.com>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/srv.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/datastore.h"
#include "asterisk/channel.h"

/*** DOCUMENTATION
	<function name="SRVQUERY" language="en_US">
		<synopsis>
			Initiate an SRV query.
		</synopsis>
		<syntax>
			<parameter name="service" required="true">
				<para>The service for which to look up SRV records. An example would be something
				like <literal>_sip._udp.example.com</literal></para>
			</parameter>
		</syntax>
		<description>
			<para>This will do an SRV lookup of the given service.</para>
		</description>
	</function>
	<function name="SRVRESULT" language="en_US">
		<synopsis>
			Retrieve results from an SRVQUERY.
		</synopsis>
		<syntax>
			<parameter name="id" required="true">
				<para>The identifier returned by the SRVQUERY function.</para>
			</parameter>
			<parameter name="resultnum" required="true">
				<para>The number of the result that you want to retrieve.</para>
				<para>Results start at <literal>1</literal>. If this argument is specified
				as <literal>getnum</literal>, then it will return the total number of results
				that are available.</para>
			</parameter>
		</syntax>
		<description>
			<para>This function will retrieve results from a previous use
			of the SRVQUERY function.</para>
		</description>
	</function>
 ***/

struct srv_result_datastore {
	struct srv_context *context;
	char id[1];
};

static void srds_destroy_cb(void *data)
{
	struct srv_result_datastore *datastore = data;
	ast_srv_cleanup(&datastore->context);
	ast_free(datastore);
}

static const struct ast_datastore_info srv_result_datastore_info = {
	.type = "SRVQUERY",
	.destroy = srds_destroy_cb,
};

static struct srv_context *srv_datastore_setup(const char *service, struct ast_channel *chan)
{
	struct srv_result_datastore *srds;
	struct ast_datastore *datastore;
	const char *host;
	unsigned short port;

	if (!(srds = ast_calloc(1, sizeof(*srds) + strlen(service)))) {
		return NULL;
	}

	ast_autoservice_start(chan);
	if (ast_srv_lookup(&srds->context, service, &host, &port) < 0) {
		ast_autoservice_stop(chan);
		ast_log(LOG_NOTICE, "Error performing lookup of service '%s'\n", service);
		ast_free(srds);
		return NULL;
	}
	ast_autoservice_stop(chan);

	strcpy(srds->id, service);

	if (!(datastore = ast_datastore_alloc(&srv_result_datastore_info, srds->id))) {
		ast_srv_cleanup(&srds->context);
		ast_free(srds);
		return NULL;
	}

	datastore->data = srds;
	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);
	return srds->context;
}

static int srv_query_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_datastore *datastore;

	if (!chan) {
		ast_log(LOG_WARNING, "%s cannot be used without a channel\n", cmd);
		return -1;
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires a service as an argument\n", cmd);
		return -1;
	}
	
	/* If they already called SRVQUERY for this service once,
	 * we need to kill the old datastore.
	 */
	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &srv_result_datastore_info, data);
	ast_channel_unlock(chan);

	if (datastore) {
		ast_channel_datastore_remove(chan, datastore);
		ast_datastore_free(datastore);
	}
	
	if (!srv_datastore_setup(data, chan)) {
		return -1;
	}

	ast_copy_string(buf, data, len);

	return 0;
}

static struct ast_custom_function srv_query_function = {
	.name = "SRVQUERY",
	.read = srv_query_read,
};

static int srv_result_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct srv_result_datastore *srds;
	struct ast_datastore *datastore;
	struct srv_context *srv_context;
	char *parse;
	const char *host;
	unsigned short port, priority, weight;
	unsigned int num;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(id);
		AST_APP_ARG(resultnum);
		AST_APP_ARG(field);
	);

	if (!chan) {
		ast_log(LOG_WARNING, "%s cannot be used without a channel\n", cmd);
		return -1;
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires two arguments (id and resultnum)\n", cmd);
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &srv_result_datastore_info, args.id);
	ast_channel_unlock(chan);

	if (!datastore) {
		/* They apparently decided to call SRVRESULT without first calling SRVQUERY.
		 * No problem, we'll do the SRV lookup now.
		 */
		srv_context = srv_datastore_setup(args.id, chan);
		if (!srv_context) {
			return -1;
		}
	} else {
		srds = datastore->data;
		srv_context = srds->context;
	}

	if (!strcasecmp(args.resultnum, "getnum")) {
		snprintf(buf, len, "%u", ast_srv_get_record_count(srv_context));
		return 0;
	}

	if (ast_strlen_zero(args.field)) {
		ast_log(LOG_ERROR, "A field must be provided when requesting SRV data\n");
		return -1;
	}

	if (sscanf(args.resultnum, "%30u", &num) != 1) {
		ast_log(LOG_ERROR, "Invalid value '%s' for resultnum to %s\n", args.resultnum, cmd);
		return -1;
	}

	if (ast_srv_get_nth_record(srv_context, num, &host, &port, &priority, &weight)) {
		ast_log(LOG_ERROR, "Failed to get record number %u for %s\n", num, cmd);
		return -1;
	}

	if (!strcasecmp(args.field, "host")) {
		ast_copy_string(buf, host, len);
	} else if (!strcasecmp(args.field, "port")) {
		snprintf(buf, len, "%u", port);
	} else if (!strcasecmp(args.field, "priority")) {
		snprintf(buf, len, "%u", priority);
	} else if (!strcasecmp(args.field, "weight")) {
		snprintf(buf, len, "%u", weight);
	} else {
		ast_log(LOG_WARNING, "Unrecognized SRV field '%s'\n", args.field);
		return -1;
	}

	return 0;
}

static struct ast_custom_function srv_result_function = {
	.name = "SRVRESULT",
	.read = srv_result_read,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&srv_query_function);
	res |= ast_custom_function_unregister(&srv_result_function);

	return res;
}

static int load_module(void)
{
	int res = ast_custom_function_register(&srv_query_function);
	if (res < 0) {
		return AST_MODULE_LOAD_DECLINE;
	}
	res = ast_custom_function_register(&srv_result_function);
	if (res < 0) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "SRV related dialplan functions");
