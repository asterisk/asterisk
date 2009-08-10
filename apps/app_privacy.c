/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Block all calls without Caller*ID, require phone # to be entered
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/utils.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/image.h"
#include "asterisk/callerid.h"
#include "asterisk/app.h"
#include "asterisk/config.h"

#define PRIV_CONFIG "privacy.conf"

static char *app = "PrivacyManager";

static char *synopsis = "Require phone number to be entered, if no CallerID sent";

static char *descrip =
  "  PrivacyManager([maxretries[|minlength[|options]]]): If no Caller*ID \n"
  "is sent, PrivacyManager answers the channel and asks the caller to\n"
  "enter their phone number. The caller is given 3 attempts to do so.\n"
  "The application does nothing if Caller*ID was received on the channel.\n"
  "  Configuration file privacy.conf contains two variables:\n"
  "   maxretries  default 3  -maximum number of attempts the caller is allowed \n"
  "               to input a callerid.\n"
  "   minlength   default 10 -minimum allowable digits in the input callerid number.\n"
  "If you don't want to use the config file and have an i/o operation with\n"
  "every call, you can also specify maxretries and minlength as application\n"
  "parameters. Doing so supercedes any values set in privacy.conf.\n"
  "The option string may contain the following character: \n"
  "  'j' -- jump to n+101 priority after <maxretries> failed attempts to collect\n"
  "         the minlength number of digits.\n"
  "The application sets the following channel variable upon completion: \n"
  "PRIVACYMGRSTATUS  The status of the privacy manager's attempt to collect \n"
  "                  a phone number from the user. A text string that is either:\n" 
  "          SUCCESS | FAILED \n"
;


static int privacy_exec (struct ast_channel *chan, void *data)
{
	int res=0;
	int retries;
	int maxretries = 3;
	int minlength = 10;
	int x = 0;
	const char *s;
	char phone[30];
	struct ast_module_user *u;
	struct ast_config *cfg = NULL;
	char *parse = NULL;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(maxretries);
		AST_APP_ARG(minlength);
		AST_APP_ARG(options);
	);

	u = ast_module_user_add(chan);

	if (!ast_strlen_zero(chan->cid.cid_num)) {
		if (option_verbose > 2)
			ast_verbose (VERBOSE_PREFIX_3 "CallerID Present: Skipping\n");
	} else {
		/*Answer the channel if it is not already*/
		if (chan->_state != AST_STATE_UP) {
			res = ast_answer(chan);
			if (res) {
				ast_module_user_remove(u);
				return -1;
			}
		}

		if (!ast_strlen_zero(data)) {
			parse = ast_strdupa(data);
			
			AST_STANDARD_APP_ARGS(args, parse);

			if (args.maxretries) {
				if (sscanf(args.maxretries, "%30d", &x) == 1)
					maxretries = x;
				else
					ast_log(LOG_WARNING, "Invalid max retries argument\n");
			}
			if (args.minlength) {
				if (sscanf(args.minlength, "%30d", &x) == 1)
					minlength = x;
				else
					ast_log(LOG_WARNING, "Invalid min length argument\n");
			}
			if (args.options)
				if (strchr(args.options, 'j'))
					priority_jump = 1;

		}		

		if (!x)
		{
			/*Read in the config file*/
			cfg = ast_config_load(PRIV_CONFIG);
		
			if (cfg && (s = ast_variable_retrieve(cfg, "general", "maxretries"))) {
				if (sscanf(s, "%30d", &x) == 1) 
					maxretries = x;
				else
					ast_log(LOG_WARNING, "Invalid max retries argument\n");
        		}

			if (cfg && (s = ast_variable_retrieve(cfg, "general", "minlength"))) {
				if (sscanf(s, "%30d", &x) == 1) 
					minlength = x;
				else
					ast_log(LOG_WARNING, "Invalid min length argument\n");
			}
		}	
		
		/*Play unidentified call*/
		res = ast_safe_sleep(chan, 1000);
		if (!res)
			res = ast_streamfile(chan, "privacy-unident", chan->language);
		if (!res)
			res = ast_waitstream(chan, "");

		/*Ask for 10 digit number, give 3 attempts*/
		for (retries = 0; retries < maxretries; retries++) {
			if (!res)
				res = ast_streamfile(chan, "privacy-prompt", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");

			if (!res ) 
				res = ast_readstring(chan, phone, sizeof(phone) - 1, /* digit timeout ms */ 3200, /* first digit timeout */ 5000, "#");

			if (res < 0)
				break;

			/*Make sure we get at least digits*/
			if (strlen(phone) >= minlength ) 
				break;
			else {
				res = ast_streamfile(chan, "privacy-incorrect", chan->language);
				if (!res)
					res = ast_waitstream(chan, "");
			}
		}
		
		/*Got a number, play sounds and send them on their way*/
		if ((retries < maxretries) && res >= 0 ) {
			res = ast_streamfile(chan, "privacy-thankyou", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");

			ast_set_callerid (chan, phone, "Privacy Manager", NULL); 

			/* Clear the unavailable presence bit so if it came in on PRI
			 * the caller id will now be passed out to other channels
			 */
			chan->cid.cid_pres &= (AST_PRES_UNAVAILABLE ^ 0xFF);

			if (option_verbose > 2) {
				ast_verbose (VERBOSE_PREFIX_3 "Changed Caller*ID to %s, callerpres to %d\n",phone,chan->cid.cid_pres);
			}
			pbx_builtin_setvar_helper(chan, "PRIVACYMGRSTATUS", "SUCCESS");
		} else {
			if (priority_jump || ast_opt_priority_jumping)	
				ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
			pbx_builtin_setvar_helper(chan, "PRIVACYMGRSTATUS", "FAILED");
		}
		if (cfg) 
			ast_config_destroy(cfg);
	}

	ast_module_user_remove(u);

	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application (app);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	return ast_register_application (app, privacy_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Require phone number to be entered, if no CallerID sent");
