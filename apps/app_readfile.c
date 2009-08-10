/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Matt O'Gorman <mogorman@digium.com>
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
 * \brief ReadFile application -- Reads in a File for you.
 *
 * \author Matt O'Gorman <mogorman@digium.com>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/module.h"

/*** DOCUMENTATION
	<application name="ReadFile" language="en_US">
		<synopsis>
			Read the contents of a text file into a channel variable.
		</synopsis>
		<syntax argsep="=">
			<parameter name="varname" required="true">
				<para>Result stored here.</para>
			</parameter>
			<parameter name="fileparams" required="true">
				<argument name="file" required="true">
					<para>The name of the file to read.</para>
				</argument>
				<argument name="length" required="false">
					<para>Maximum number of characters to capture.</para>
					<para>If not specified defaults to max.</para>
				</argument>
			</parameter>
		</syntax>
		<description>
			<para>Read the contents of a text file into channel variable <replaceable>varname</replaceable></para>
			<warning><para>ReadFile has been deprecated in favor of Set(varname=${FILE(file,0,length)})</para></warning>
		</description>
		<see-also>
			<ref type="application">System</ref>
			<ref type="application">Read</ref>
		</see-also>
	</application>
 ***/

static char *app_readfile = "ReadFile";

static int readfile_exec(struct ast_channel *chan, const char *data)
{
	int res=0;
	char *s, *varname=NULL, *file=NULL, *length=NULL, *returnvar=NULL;
	int len=0;
	static int deprecation_warning = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ReadFile require an argument!\n");
		return -1;
	}

	s = ast_strdupa(data);

	varname = strsep(&s, "=");
	file = strsep(&s, ",");
	length = s;

	if (deprecation_warning++ % 10 == 0)
		ast_log(LOG_WARNING, "ReadFile has been deprecated in favor of Set(%s=${FILE(%s,0,%s)})\n", varname, file, length);

	if (!varname || !file) {
		ast_log(LOG_ERROR, "No file or variable specified!\n");
		return -1;
	}

	if (length) {
		if ((sscanf(length, "%30d", &len) != 1) || (len < 0)) {
			ast_log(LOG_WARNING, "%s is not a positive number, defaulting length to max\n", length);
			len = 0;
		}
	}

	if ((returnvar = ast_read_textfile(file))) {
		if (len > 0) {
			if (len < strlen(returnvar))
				returnvar[len]='\0';
			else
				ast_log(LOG_WARNING, "%s is longer than %d, and %d \n", file, len, (int)strlen(returnvar));
		}
		pbx_builtin_setvar_helper(chan, varname, returnvar);
		ast_free(returnvar);
	}

	return res;
}


static int unload_module(void)
{
	return ast_unregister_application(app_readfile);
}

static int load_module(void)
{
	return ast_register_application_xml(app_readfile, readfile_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Stores output of file into a variable");
