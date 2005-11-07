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
 * \ingroup applications
 */

#include <string.h>
#include <stdlib.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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

static char *tdesc = "Require phone number to be entered, if no CallerID sent";

static char *app = "PrivacyManager";

static char *synopsis = "Require phone number to be entered, if no CallerID sent";

static char *descrip =
  "  PrivacyManager: If no Caller*ID is sent, PrivacyManager answers the\n"
  "channel and asks the caller to enter their phone number.\n"
  "The caller is given 3 attempts.  If after 3 attempts, they do not enter\n"
  "at least a 10 digit phone number, and if there exists a priority n + 101,\n"
  "where 'n' is the priority of the current instance, then  the\n"
  "channel  will  be  setup  to continue at that priority level.\n"
  "Otherwise, the call is hungup.  Does nothing if Caller*ID was received on the\n"
  "channel.\n"
  "  Configuration file privacy.conf contains two variables:\n"
  "   maxretries  default 3  -maximum number of attempts the caller is allowed to input a callerid.\n"
  "   minlength   default 10 -minimum allowable digits in the input callerid number.\n"
;

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;



static int privacy_exec (struct ast_channel *chan, void *data)
{
	int res=0;
	int retries;
	int maxretries = 3;
	int minlength = 10;
	int x;
	char *s;
	char phone[30];
	struct localuser *u;
	struct ast_config *cfg;

	LOCAL_USER_ADD (u);
	if (!ast_strlen_zero(chan->cid.cid_num)) {
		if (option_verbose > 2)
			ast_verbose (VERBOSE_PREFIX_3 "CallerID Present: Skipping\n");
	} else {
		/*Answer the channel if it is not already*/
		if (chan->_state != AST_STATE_UP) {
			res = ast_answer(chan);
			if (res) {
				LOCAL_USER_REMOVE(u);
				return -1;
			}
		}
		/*Read in the config file*/
		cfg = ast_config_load(PRIV_CONFIG);
		
		
		/*Play unidentified call*/
		res = ast_safe_sleep(chan, 1000);
		if (!res)
			res = ast_streamfile(chan, "privacy-unident", chan->language);
		if (!res)
			res = ast_waitstream(chan, "");

        if (cfg && (s = ast_variable_retrieve(cfg, "general", "maxretries"))) {
                if (sscanf(s, "%d", &x) == 1) {
                        maxretries = x;
                } else {
                        ast_log(LOG_WARNING, "Invalid max retries argument\n");
                }
        }
        if (cfg && (s = ast_variable_retrieve(cfg, "general", "minlength"))) {
                if (sscanf(s, "%d", &x) == 1) {
                        minlength = x;
                } else {
                        ast_log(LOG_WARNING, "Invalid min length argument\n");
                }
        }
			
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
		if ((retries < maxretries) && res == 1 ) {
			res = ast_streamfile(chan, "privacy-thankyou", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");
			ast_set_callerid (chan, phone, "Privacy Manager", NULL);
			if (option_verbose > 2)
				ast_verbose (VERBOSE_PREFIX_3 "Changed Caller*ID to %s\n",phone);
		} else {
			/* Send the call to n+101 priority, where n is the current priority  */
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		}
		if (cfg) 
			ast_config_destroy(cfg);
	}

  LOCAL_USER_REMOVE (u);
  return 0;
}

int
unload_module (void)
{
	int res;

	res = ast_unregister_application (app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int
load_module (void)
{
  return ast_register_application (app, privacy_exec, synopsis,
				   descrip);
}

char *
description (void)
{
  return tdesc;
}

int
usecount (void)
{
  int res;
  STANDARD_USECOUNT (res);
  return res;
}

char *
key ()
{
  return ASTERISK_GPL_KEY;
}
