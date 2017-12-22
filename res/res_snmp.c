/*
 * Copyright (C) 2006 Voop as
 * Thorsten Lockert <tholo@voop.as>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief SNMP Agent / SubAgent support for Asterisk
 *
 * \author Thorsten Lockert <tholo@voop.as>
 *
 * Uses the Net-SNMP libraries available at
 *	 http://net-snmp.sourceforge.net/
 */

/*! \li \ref res_snmp.c uses the configuration file \ref res_snmp.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page res_snmp.conf res_snmp.conf
 * \verbinclude res_snmp.conf.sample
 */

/*** MODULEINFO
	<depend>netsnmp</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/channel.h"
#include "asterisk/module.h"

#include "snmp/agent.h"

#define	MODULE_DESCRIPTION	"SNMP [Sub]Agent for Asterisk"

int res_snmp_agentx_subagent;
int res_snmp_dont_stop;
static int res_snmp_enabled;

static pthread_t thread = AST_PTHREADT_NULL;

/*!
 * \brief Load res_snmp.conf config file
 * \return 1 on load, 0 file does not exist
*/
static int load_config(void)
{
	struct ast_variable *var;
	struct ast_config *cfg;
	struct ast_flags config_flags = { 0 };
	char *cat;

	res_snmp_enabled = 0;
	res_snmp_agentx_subagent = 1;
	cfg = ast_config_load("res_snmp.conf", config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Could not load res_snmp.conf\n");
		return 0;
	}
	cat = ast_category_browse(cfg, NULL);
	while (cat) {
		var = ast_variable_browse(cfg, cat);

		if (strcasecmp(cat, "general") == 0) {
			while (var) {
				if (strcasecmp(var->name, "subagent") == 0) {
					if (ast_true(var->value))
						res_snmp_agentx_subagent = 1;
					else if (ast_false(var->value))
						res_snmp_agentx_subagent = 0;
					else {
						ast_log(LOG_ERROR, "Value '%s' does not evaluate to true or false.\n", var->value);
						ast_config_destroy(cfg);
						return 1;
					}
				} else if (strcasecmp(var->name, "enabled") == 0) {
					res_snmp_enabled = ast_true(var->value);
				} else {
					ast_log(LOG_ERROR, "Unrecognized variable '%s' in category '%s'\n", var->name, cat);
					ast_config_destroy(cfg);
					return 1;
				}
				var = var->next;
			}
		} else {
			ast_log(LOG_ERROR, "Unrecognized category '%s'\n", cat);
			ast_config_destroy(cfg);
			return 1;
		}

		cat = ast_category_browse(cfg, cat);
	}
	ast_config_destroy(cfg);
	return 1;
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the
 * configuration file or other non-critical problem return
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	if(!load_config())
		return AST_MODULE_LOAD_DECLINE;

	ast_verb(1, "Loading [Sub]Agent Module\n");

	res_snmp_dont_stop = 1;
	if (res_snmp_enabled)
		return ast_pthread_create_background(&thread, NULL, agent_thread, NULL);
	else
		return 0;
}

static int unload_module(void)
{
	ast_verb(1, "Unloading [Sub]Agent Module\n");

	res_snmp_dont_stop = 0;
	return ((thread != AST_PTHREADT_NULL) ? pthread_join(thread, NULL) : 0);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "SNMP [Sub]Agent for Asterisk",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
);
