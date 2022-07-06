/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
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
 * \brief Reload Asterisk modules
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<application name="Reload" language="en_US">
		<since>
			<version>16.20.0</version>
			<version>18.6.0</version>
			<version>19.0.0</version>
		</since>
		<synopsis>
			Reloads an Asterisk module, blocking the channel until the reload has completed.
		</synopsis>
		<syntax>
			<parameter name="module" required="false">
				<para>The full name(s) of the target module(s) or resource(s) to reload.
				If omitted, everything will be reloaded.</para>
				<para>The full names MUST be specified (e.g. <literal>chan_iax2</literal>
				to reload IAX2 or <literal>pbx_config</literal> to reload the dialplan.</para>
			</parameter>
		</syntax>
		<description>
			<para>Reloads the specified (or all) Asterisk modules and reports success or failure.
			Success is determined by each individual module, and if all reloads are successful,
			that is considered an aggregate success. If multiple modules are specified and any
			module fails, then FAILURE will be returned. It is still possible that other modules
			did successfully reload, however.</para>
			<para>Sets <variable>RELOADSTATUS</variable> to one of the following values:</para>
			<variablelist>
				<variable name="RELOADSTATUS">
					<value name="SUCCESS">
						Specified module(s) reloaded successfully.
					</value>
					<value name="FAILURE">
						Some or all of the specified modules failed to reload.
					</value>
				</variable>
			</variablelist>
		</description>
	</application>
 ***/

static char *app = "Reload";

static int reload_exec(struct ast_channel *chan, const char *data)
{
	char *targets, *target = NULL;
	enum ast_module_reload_result res = AST_MODULE_RELOAD_SUCCESS;

	targets = ast_strdupa(data);
	ast_autoservice_start(chan);
	if (ast_strlen_zero(targets)) { /* Reload everything */
		res = ast_module_reload(targets);
	} else {
		while((target = ast_strsep(&targets, ',', AST_STRSEP_ALL))) {
			res |= ast_module_reload(target);
		}
	}
	ast_autoservice_stop(chan);

	if (res == AST_MODULE_RELOAD_SUCCESS) {
		pbx_builtin_setvar_helper(chan, "RELOADSTATUS", "SUCCESS");
	} else {
		pbx_builtin_setvar_helper(chan, "RELOADSTATUS", "FAILURE");
	}
	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, reload_exec);
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Reload module(s)");
