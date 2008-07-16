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
	.synopsis = "Returns system information specified by parameter.",
	.syntax = "SYSINFO(<parameter>)",
	.read = sysinfo_helper,
	.desc = 
"Returns information from a given parameter\n"
"  Options:\n"
"    loadavg   - system load average from past minute\n"
"    numcalls  - number of active calls currently in progress\n"
#if defined(HAVE_SYSINFO)
"    uptime    - system uptime in hours\n"
"    totalram  - total usable main memory size in KiB\n"
"    freeram   - available memory size in KiB\n"
"    bufferram - memory used by buffers in KiB\n"
"    totalswap - total swap space size in KiB\n"
"    freeswap  - free swap space still available in KiB\n"
"    numprocs  - number of current processes\n",
#endif /* HAVE_SYSINFO */
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

