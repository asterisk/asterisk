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
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"

#include "snmp/agent.h"

#define	MODULE_DESCRIPTION	"SNMP [Sub]Agent for Asterisk"

int res_snmp_agentx_subagent;
int res_snmp_dont_stop;
int res_snmp_enabled;

static pthread_t thread;

static int load_config(void)
{
    struct ast_variable *var;
    struct ast_config *cfg;
    char *cat;
	
	res_snmp_enabled = 0;
	res_snmp_agentx_subagent = 1;
    cfg = ast_config_load("res_snmp.conf");
    if (cfg) {
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
							ast_log(LOG_ERROR, "Value '%s' does not evaluate to true or false.\n",
									var->value);
							ast_config_destroy(cfg);
							return 1;
						}
					} else if (strcasecmp(var->name, "enabled") == 0) {
						res_snmp_enabled = ast_true(var->value);
					} else {
						ast_log(LOG_ERROR, "Unrecognized variable '%s' in category '%s'\n",
								var->name, cat);
						ast_config_destroy(cfg);
						return 1;
					}
					var = var->next;
				}
			}
			else {
				ast_log(LOG_ERROR, "Unrecognized category '%s'\n", cat);
				ast_config_destroy(cfg);
				return 1;
			}

			cat = ast_category_browse(cfg, cat);
		}
		ast_config_destroy(cfg);
    }

    return 0;
}

int load_module(void)
{
    load_config();

    ast_verbose(VERBOSE_PREFIX_1 "Loading [Sub]Agent Module\n");

    res_snmp_dont_stop = 1;
	if (res_snmp_enabled)
	    return ast_pthread_create(&thread, NULL, agent_thread, NULL);
	else
		return 0;
}

int unload_module(void)
{
    ast_verbose(VERBOSE_PREFIX_1 "Unloading [Sub]Agent Module\n");

    res_snmp_dont_stop = 0;
    return pthread_join(thread, NULL);
}

int reload(void)
{
    ast_verbose(VERBOSE_PREFIX_1 "Reloading [Sub]Agent Module\n");

    res_snmp_dont_stop = 0;
    pthread_join(thread, NULL);

    load_config();

    res_snmp_dont_stop = 1;
	if (res_snmp_enabled)
	    return ast_pthread_create(&thread, NULL, agent_thread, NULL);
	else
		return 0;
}

int usecount(void)
{
    return 0;
}

char *key(void)
{
    return ASTERISK_GPL_KEY;
}

char *description(void)
{
    return MODULE_DESCRIPTION;
}

/*
 * Local Variables:
 * c-file-style: gnu
 * c-basic-offset: 4
 * c-file-offsets: ((case-label . 0))
 * tab-width: 4
 * indent-tabs-mode: t
 * End:
 */
