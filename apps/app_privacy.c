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

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/utils.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/image.h"
#include "asterisk/callerid.h"
#include "asterisk/app.h"
#include "asterisk/config.h"

/*** DOCUMENTATION
	<application name="PrivacyManager" language="en_US">
		<synopsis>
			Require phone number to be entered, if no CallerID sent
		</synopsis>
		<syntax>
			<parameter name="maxretries">
				<para>Total tries caller is allowed to input a callerid. Defaults to <literal>3</literal>.</para>
			</parameter>
			<parameter name="minlength">
				<para>Minimum allowable digits in the input callerid number. Defaults to <literal>10</literal>.</para>
			</parameter>
			<parameter name="context">
				<para>Context to check the given callerid against patterns.</para>
			</parameter>
		</syntax>
		<description>
			<para>If no Caller*ID is sent, PrivacyManager answers the channel and asks
			the caller to enter their phone number. The caller is given
			<replaceable>maxretries</replaceable> attempts to do so. The application does
			<emphasis>nothing</emphasis> if Caller*ID was received on the channel.</para>
			<para>The application sets the following channel variable upon completion:</para>
			<variablelist>
				<variable name="PRIVACYMGRSTATUS">
					<para>The status of the privacy manager's attempt to collect a phone number from the user.</para>
					<value name="SUCCESS"/>
					<value name="FAILED"/>
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">Zapateller</ref>
		</see-also>
	</application>
 ***/


static char *app = "PrivacyManager";

static int privacy_exec(struct ast_channel *chan, const char *data)
{
	int res=0;
	int retries;
	int maxretries = 3;
	int minlength = 10;
	int x = 0;
	char phone[30];
	char *parse = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(maxretries);
		AST_APP_ARG(minlength);
		AST_APP_ARG(options);
		AST_APP_ARG(checkcontext);
	);

	if (!ast_strlen_zero(chan->cid.cid_num)) {
		ast_verb(3, "CallerID Present: Skipping\n");
	} else {
		/*Answer the channel if it is not already*/
		if (chan->_state != AST_STATE_UP) {
			if ((res = ast_answer(chan)))
				return -1;
		}

		if (!ast_strlen_zero(data)) {
			parse = ast_strdupa(data);
			
			AST_STANDARD_APP_ARGS(args, parse);

			if (args.maxretries) {
				if (sscanf(args.maxretries, "%d", &x) == 1)
					maxretries = x;
				else
					ast_log(LOG_WARNING, "Invalid max retries argument\n");
			}
			if (args.minlength) {
				if (sscanf(args.minlength, "%d", &x) == 1)
					minlength = x;
				else
					ast_log(LOG_WARNING, "Invalid min length argument\n");
			}
		}		

		/* Play unidentified call */
		res = ast_safe_sleep(chan, 1000);
		if (!res)
			res = ast_streamfile(chan, "privacy-unident", chan->language);
		if (!res)
			res = ast_waitstream(chan, "");

		/* Ask for 10 digit number, give 3 attempts */
		for (retries = 0; retries < maxretries; retries++) {
			if (!res)
				res = ast_streamfile(chan, "privacy-prompt", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");

			if (!res ) 
				res = ast_readstring(chan, phone, sizeof(phone) - 1, /* digit timeout ms */ 3200, /* first digit timeout */ 5000, "#");

			if (res < 0)
				break;

			/* Make sure we get at least digits */
			if (strlen(phone) >= minlength ) {
				/* if we have a checkcontext argument, do pattern matching */
				if (!ast_strlen_zero(args.checkcontext)) {
					if (!ast_exists_extension(NULL, args.checkcontext, phone, 1, NULL)) {
						res = ast_streamfile(chan, "privacy-incorrect", chan->language);
						if (!res) {
							res = ast_waitstream(chan, "");
						}
					} else {
						break;
					}
				} else {
					break;
				}
			} else {
				res = ast_streamfile(chan, "privacy-incorrect", chan->language);
				if (!res)
					res = ast_waitstream(chan, "");
			}
		}
		
		/* Got a number, play sounds and send them on their way */
		if ((retries < maxretries) && res >= 0 ) {
			res = ast_streamfile(chan, "privacy-thankyou", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");

			ast_set_callerid (chan, phone, "Privacy Manager", NULL); 

			/* Clear the unavailable presence bit so if it came in on PRI
			 * the caller id will now be passed out to other channels
			 */
			chan->cid.cid_pres &= (AST_PRES_UNAVAILABLE ^ 0xFF);

			ast_verb(3, "Changed Caller*ID to %s, callerpres to %d\n",phone,chan->cid.cid_pres);

			pbx_builtin_setvar_helper(chan, "PRIVACYMGRSTATUS", "SUCCESS");
		} else {
			pbx_builtin_setvar_helper(chan, "PRIVACYMGRSTATUS", "FAILED");
		}
	}

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application (app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, privacy_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Require phone number to be entered, if no CallerID sent");
