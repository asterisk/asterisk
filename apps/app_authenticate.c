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
 * \brief Execute arbitrary authenticate commands
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/astdb.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"

enum {
	OPT_ACCOUNT = (1 << 0),
	OPT_DATABASE = (1 << 1),
	OPT_JUMP = (1 << 2),
	OPT_MULTIPLE = (1 << 3),
	OPT_REMOVE = (1 << 4),
} auth_option_flags;

AST_APP_OPTIONS(auth_app_options, {
	AST_APP_OPTION('a', OPT_ACCOUNT),
	AST_APP_OPTION('d', OPT_DATABASE),
	AST_APP_OPTION('j', OPT_JUMP),
	AST_APP_OPTION('m', OPT_MULTIPLE),
	AST_APP_OPTION('r', OPT_REMOVE),
});

static char *tdesc = "Authentication Application";

static char *app = "Authenticate";

static char *synopsis = "Authenticate a user";

static char *descrip =
"  Authenticate(password[|options[|maxdigits]]): This application asks the caller\n"
"to enter a given password in order to continue dialplan execution. If the password\n"
"begins with the '/' character, it is interpreted as a file which contains a list of\n"
"valid passwords, listed 1 password per line in the file.\n"
"  When using a database key, the value associated with the key can be anything.\n"
"Users have three attempts to authenticate before the channel is hung up. If the\n"
"passsword is invalid, the 'j' option is specified, and priority n+101 exists,\n"
"dialplan execution will continnue at this location.\n"
"  Options:\n"
"     a - Set the channels' account code to the password that is entered\n"
"     d - Interpret the given path as database key, not a literal file\n"
"     j - Support jumping to n+101 if authentication fails\n"
"     m - Interpret the given path as a file which contains a list of account\n"
"         codes and password hashes delimited with ':', listed one per line in\n"
"         the file. When one of the passwords is matched, the channel will have\n"
"         its account code set to the corresponding account code in the file.\n"
"     r - Remove the database key upon successful entry (valid with 'd' only)\n"
"     maxdigits  - maximum acceptable number of digits. Stops reading after\n"
"         maxdigits have been entered (without requiring the user to\n"
"         press the '#' key).\n"
"         Defaults to 0 - no limit - wait for the user press the '#' key.\n"
;

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int auth_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	int retries;
	struct localuser *u;
	char passwd[256];
	char *prompt;
	int maxdigits;
	char *argcopy =NULL;
	struct ast_flags flags = {0};

	AST_DECLARE_APP_ARGS(arglist,
		AST_APP_ARG(password);
		AST_APP_ARG(options);
		AST_APP_ARG(maxdigits);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Authenticate requires an argument(password)\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u);

	if (chan->_state != AST_STATE_UP) {
		res = ast_answer(chan);
		if (res) {
			LOCAL_USER_REMOVE(u);
			return -1;
		}
	}
	
	if (!(argcopy = ast_strdupa(data))) {
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(arglist,argcopy);
	
	if (!ast_strlen_zero(arglist.options)) {
		ast_app_parse_options(auth_app_options, &flags, NULL, arglist.options);
	}

	if (!ast_strlen_zero(arglist.maxdigits)) {
		maxdigits = atoi(arglist.maxdigits);
		if ((maxdigits<1) || (maxdigits>sizeof(passwd)-2))
			maxdigits = sizeof(passwd) - 2;
	} else {
		maxdigits = sizeof(passwd) - 2;
	}

	/* Start asking for password */
	prompt = "agent-pass";
	for (retries = 0; retries < 3; retries++) {
		res = ast_app_getdata(chan, prompt, passwd, maxdigits, 0);
		if (res < 0)
			break;
		res = 0;
		if (arglist.password[0] == '/') {
			if (ast_test_flag(&flags,OPT_DATABASE)) {
				char tmp[256];
				/* Compare against a database key */
				if (!ast_db_get(arglist.password + 1, passwd, tmp, sizeof(tmp))) {
					/* It's a good password */
					if (ast_test_flag(&flags,OPT_REMOVE)) {
						ast_db_del(arglist.password + 1, passwd);
					}
					break;
				}
			} else {
				/* Compare against a file */
				FILE *f;
				f = fopen(arglist.password, "r");
				if (f) {
					char buf[256] = "";
					char md5passwd[33] = "";
					char *md5secret = NULL;

					while (!feof(f)) {
						fgets(buf, sizeof(buf), f);
						if (!feof(f) && !ast_strlen_zero(buf)) {
							buf[strlen(buf) - 1] = '\0';
							if (ast_test_flag(&flags,OPT_MULTIPLE)) {
								md5secret = strchr(buf, ':');
								if (md5secret == NULL)
									continue;
								*md5secret = '\0';
								md5secret++;
								ast_md5_hash(md5passwd, passwd);
								if (!strcmp(md5passwd, md5secret)) {
									if (ast_test_flag(&flags,OPT_ACCOUNT))
										ast_cdr_setaccount(chan, buf);
									break;
								}
							} else {
								if (!strcmp(passwd, buf)) {
									if (ast_test_flag(&flags,OPT_ACCOUNT))
										ast_cdr_setaccount(chan, buf);
									break;
								}
							}
						}
					}
					fclose(f);
					if (!ast_strlen_zero(buf)) {
						if (ast_test_flag(&flags,OPT_MULTIPLE)) {
							if (md5secret && !strcmp(md5passwd, md5secret))
								break;
						} else {
							if (!strcmp(passwd, buf))
								break;
						}
					}
				} else 
					ast_log(LOG_WARNING, "Unable to open file '%s' for authentication: %s\n", arglist.password, strerror(errno));
			}
		} else {
			/* Compare against a fixed password */
			if (!strcmp(passwd, arglist.password)) 
				break;
		}
		prompt="auth-incorrect";
	}
	if ((retries < 3) && !res) {
		if (ast_test_flag(&flags,OPT_ACCOUNT) && !ast_test_flag(&flags,OPT_MULTIPLE)) 
			ast_cdr_setaccount(chan, passwd);
		res = ast_streamfile(chan, "auth-thankyou", chan->language);
		if (!res)
			res = ast_waitstream(chan, "");
	} else {
		if (ast_test_flag(&flags,OPT_JUMP) && ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) {
			res = 0;
		} else {
			if (!ast_streamfile(chan, "vm-goodbye", chan->language))
				res = ast_waitstream(chan, "");
			res = -1;
		}
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;
	
	return res;
}

int load_module(void)
{
	return ast_register_application(app, auth_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
