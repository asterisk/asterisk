/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
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
 * SYSINFO function to return various system data.
 * 
 * \note Inspiration and Guidance from Russell
 *
 * \author Jeff Peeler
 *
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 87233 $")

#if defined(HAVE_SYSINFO)
#include <sys/sysinfo.h>
#endif

#include "asterisk/module.h"
#include "asterisk/pbx.h"

/*** DOCUMENTATION
	<function name="SYSINFO" language="en_US">
		<synopsis>
			Returns system information specified by parameter.
		</synopsis>
		<syntax>
			<parameter name="parameter" required="true">
				<enumlist>
					<enum name="loadavg">
						<para>System load average from past minute.</para>
					</enum>
					<enum name="numcalls">
						<para>Number of active calls currently in progress.</para>
					</enum>
					<enum name="uptime">
						<para>System uptime in hours.</para>
						<note><para>This parameter is dependant upon operating system.</para></note>
					</enum>
					<enum name="totalram">
						<para>Total usable main memory size in KiB.</para>
						<note><para>This parameter is dependant upon operating system.</para></note>
					</enum>
					<enum name="freeram">
						<para>Available memory size in KiB.</para>
						<note><para>This parameter is dependant upon operating system.</para></note>
					</enum>
					<enum name="bufferram">
						<para>Memory used by buffers in KiB.</para>
						<note><para>This parameter is dependant upon operating system.</para></note>
					</enum>
					<enum name="totalswap">
						<para>Total swap space still available in KiB.</para>
						<note><para>This parameter is dependant upon operating system.</para></note>
					</enum>
					<enum name="freeswap">
						<para>Free swap space still available in KiB.</para>
						<note><para>This parameter is dependant upon operating system.</para></note>
					</enum>
					<enum name="numprocs">
						<para>Number of current processes.</para>
						<note><para>This parameter is dependant upon operating system.</para></note>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Returns information from a given parameter.</para>
		</description>
	</function>
 ***/

static int sysinfo_helper(struct ast_channel *chan, const char *cmd, char *data,
		                         char *buf, size_t len)
{
#if defined(HAVE_SYSINFO)
	struct sysinfo sys_info;
	if (sysinfo(&sys_info)) {
		ast_log(LOG_ERROR, "FAILED to retrieve system information\n");
		return -1;
	}
#endif
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: ${SYSINFO(<parameter>)} - missing argument!)\n");
		return -1;
	} else if (!strcasecmp("loadavg", data)) {
		double curloadavg;
		getloadavg(&curloadavg, 1);
		snprintf(buf, len, "%f", curloadavg);
	} else if (!strcasecmp("numcalls", data)) {
		snprintf(buf, len, "%d", ast_active_calls());
	}
#if defined(HAVE_SYSINFO)
	else if (!strcasecmp("uptime", data)) {             /* in hours */
		snprintf(buf, len, "%ld", sys_info.uptime/3600);
	} else if (!strcasecmp("totalram", data)) {         /* in KiB */
		snprintf(buf, len, "%ld",(sys_info.totalram * sys_info.mem_unit)/1024);
	} else if (!strcasecmp("freeram", data)) {          /* in KiB */
		snprintf(buf, len, "%ld",(sys_info.freeram * sys_info.mem_unit)/1024);
	} else if (!strcasecmp("bufferram", data)) {        /* in KiB */
		snprintf(buf, len, "%ld",(sys_info.bufferram * sys_info.mem_unit)/1024);
	} else if (!strcasecmp("totalswap", data)) {        /* in KiB */
		snprintf(buf, len, "%ld",(sys_info.totalswap * sys_info.mem_unit)/1024);
	} else if (!strcasecmp("freeswap", data)) {         /* in KiB */
		snprintf(buf, len, "%ld",(sys_info.freeswap * sys_info.mem_unit)/1024);
	} else if (!strcasecmp("numprocs", data)) {
		snprintf(buf, len, "%d", sys_info.procs);
	}
#endif
 	else {
		ast_log(LOG_ERROR, "Unknown sysinfo parameter type '%s'.\n", data);
		return -1;
	}
		
	return 0;
}

static struct ast_custom_function sysinfo_function = {
	.name = "SYSINFO",
	.read = sysinfo_helper,
	.read_max = 22,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&sysinfo_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&sysinfo_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "System information related functions");

