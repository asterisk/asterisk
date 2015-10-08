/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Tyler Cambron <tcambron@digium.com>
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

 /*** MODULEINFO
	<depend>res_statsd</depend>
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/logger.h"

/*** DOCUMENTATION
	<application name="Statsd" language="en_US">
		<synopsis>
			Allow statistics to be passed to the StatsD server from the dialplan.
		</synopsis>
		<syntax>
			<parameter name="metric_type" required="true">
				<para>The metric type to be sent to StatsD.</para>
			</parameter>
			<parameter name="statistic_name" required="true">
				<para>The name of the variable to be sent to StatsD.</para>
			</parameter>
			<parameter name="value" required="true">
				<para>The value of the variable to be sent to StatsD.</para>
			</parameter>
		</syntax>
		<description>
			<para>This dialplan application sends statistics to the StatsD server
			specified inside of <literal>statsd.conf</literal>.</para>
		</description>
	</application>
 ***/

static const char app[] = "Statsd";

static int statsd_exec(struct ast_channel *chan, const char *data)
{
	ast_log(LOG_NOTICE, "StatsD application callback is working!\n");
	return 0;
}


static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, statsd_exec);
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Statsd Dialplan Application");