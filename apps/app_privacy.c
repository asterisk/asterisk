/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Block all calls without Caller*ID, require phone # to be entered
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/image.h>
#include <asterisk/callerid.h>
#include <asterisk/app.h>
#include <asterisk/config.h>
#include <string.h>
#include <stdlib.h>

#define PRIV_CONFIG "privacy.conf"

static char *tdesc = "Require phone number to be entered, if no CallerID sent";

static char *app = "PrivacyManager";

static char *synopsis = "Require phone number to be entered, if no CallerID sent";

static char *descrip =
  "  PrivacyManager: If no Caller*ID is sent, PrivacyManager answers the\n"
  "channel and asks the caller to enter their 10 digit phone number.\n"
  "The caller is given 3 attempts.  If after 3 attempts, they do not enter\n"
  "their 10 digit phone number, and if there exists a priority n + 101,\n"
  "where 'n' is the priority of the current instance, then  the\n"
  "channel  will  be  setup  to continue at that priority level.\n"
  "Otherwise, it returns 0.  Does nothing if Caller*ID was received on the\n"
  "channel.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int
privacy_exec (struct ast_channel *chan, void *data)
{
	int res=0;
	int retries;
	int maxretries = 3;
	int x;
	char *s;
	char phone[10];
	char new_cid[144];
	struct localuser *u;
	struct ast_config *cfg;

	LOCAL_USER_ADD (u);
	if (chan->callerid)
	{
		if (option_verbose > 2)
			ast_verbose (VERBOSE_PREFIX_3 "CallerID Present: Skipping\n");
	}
	else
	{
		/*Answer the channel if it is not already*/
		if (chan->_state != AST_STATE_UP) {
			res = ast_answer(chan);
			if (res) {
				LOCAL_USER_REMOVE(u);
				return -1;
			}
		}
		/*Read in the config file*/
		cfg = ast_load(PRIV_CONFIG);
		
		
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
			
		/*Ask for 10 digit number, give 3 attempts*/
		for (retries = 0; retries < maxretries; retries++) {
			if (!res)
				res = ast_app_getdata(chan, "privacy-prompt", phone, sizeof(phone), 0);
			if (res < 0)
				break;

			/*Make sure we get 10 digits*/
			if (strlen(phone) == 10) 
				break;
			else {
				res = ast_streamfile(chan, "privacy-incorrect", chan->language);
				if (!res)
					res = ast_waitstream(chan, "");
			}
		}
		
		/*Got a number, play sounds and send them on their way*/
		if ((retries < maxretries) && !res) {
			res = ast_streamfile(chan, "privacy-thankyou", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");
			snprintf (new_cid, sizeof (new_cid), "\"%s\" <%s>", "Privacy Manager", phone);
			ast_set_callerid (chan, new_cid, 0);
			if (option_verbose > 2)
				ast_verbose (VERBOSE_PREFIX_3 "Changed Caller*ID to %s\n",new_cid);
		} else {
			/*Send the call to n+101 priority, where n is the current priority*/
			if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->callerid))
				chan->priority+=100;
		}
		if (cfg) 
			ast_destroy(cfg);
	}

  LOCAL_USER_REMOVE (u);
  return 0;
}

int
unload_module (void)
{
  STANDARD_HANGUP_LOCALUSERS;
  return ast_unregister_application (app);
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
