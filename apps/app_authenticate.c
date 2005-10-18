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

/*
 *
 * Execute arbitrary authenticate commands
 * 
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

static char *tdesc = "Authentication Application";

static char *app = "Authenticate";

static char *synopsis = "Authenticate a user";

static char *descrip =
"  Authenticate(password[|options]): Requires a user to enter a"
"given password in order to continue execution.  If the\n"
"password begins with the '/' character, it is interpreted as\n"
"a file which contains a list of valid passwords (1 per line).\n"
"an optional set of opions may be provided by concatenating any\n"
"of the following letters:\n"
"     a - Set account code to the password that is entered\n"
"     d - Interpret path as database key, not literal file\n"
"     m - Interpret path as a file which contains a list of\n"
"         account codes and password hashes delimited with ':'\n"
"         one per line. When password matched, corresponding\n"
"         account code will be set\n"
"     j - Support jumping to n+101\n"
"     r - Remove database key upon successful entry (valid with 'd' only)\n"
"\n"
"When using a database key, the value associated with the key can be\n"
"anything.\n"
"Returns 0 if the user enters a valid password within three\n"
"tries, or -1 on hangup.  If the priority n+101 exists and invalid\n"
"authentication was entered, and the 'j' flag was specified, processing\n"
"will jump to n+101 and 0 will be returned.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int auth_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	int jump = 0;
	int retries;
	struct localuser *u;
	char password[256]="";
	char passwd[256];
	char *opts;
	char *prompt;
	if (!data || ast_strlen_zero(data)) {
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
	strncpy(password, data, sizeof(password) - 1);
	opts=strchr(password, '|');
	if (opts) {
		*opts = 0;
		opts++;
	} else
		opts = "";
	if (strchr(opts, 'j'))
		jump = 1;
	/* Start asking for password */
	prompt = "agent-pass";
	for (retries = 0; retries < 3; retries++) {
		res = ast_app_getdata(chan, prompt, passwd, sizeof(passwd) - 2, 0);
		if (res < 0)
			break;
		res = 0;
		if (password[0] == '/') {
			if (strchr(opts, 'd')) {
				char tmp[256];
				/* Compare against a database key */
				if (!ast_db_get(password + 1, passwd, tmp, sizeof(tmp))) {
					/* It's a good password */
					if (strchr(opts, 'r')) {
						ast_db_del(password + 1, passwd);
					}
					break;
				}
			} else {
				/* Compare against a file */
				FILE *f;
				f = fopen(password, "r");
				if (f) {
					char buf[256] = "";
					char md5passwd[33] = "";
					char *md5secret = NULL;

					while (!feof(f)) {
						fgets(buf, sizeof(buf), f);
						if (!feof(f) && !ast_strlen_zero(buf)) {
							buf[strlen(buf) - 1] = '\0';
							if (strchr(opts, 'm')) {
								md5secret = strchr(buf, ':');
								if (md5secret == NULL)
									continue;
								*md5secret = '\0';
								md5secret++;
								ast_md5_hash(md5passwd, passwd);
								if (!strcmp(md5passwd, md5secret)) {
									if (strchr(opts, 'a'))
										ast_cdr_setaccount(chan, buf);
									break;
								}
							} else {
								if (!strcmp(passwd, buf)) {
									if (strchr(opts, 'a'))
										ast_cdr_setaccount(chan, buf);
									break;
								}
							}
						}
					}
					fclose(f);
					if (!ast_strlen_zero(buf)) {
						if (strchr(opts, 'm')) {
							if (md5secret && !strcmp(md5passwd, md5secret))
								break;
						} else {
							if (!strcmp(passwd, buf))
								break;
						}
					}
				} else 
					ast_log(LOG_WARNING, "Unable to open file '%s' for authentication: %s\n", password, strerror(errno));
			}
		} else {
			/* Compare against a fixed password */
			if (!strcmp(passwd, password)) 
				break;
		}
		prompt="auth-incorrect";
	}
	if ((retries < 3) && !res) {
		if (strchr(opts, 'a') && !strchr(opts, 'm')) 
			ast_cdr_setaccount(chan, passwd);
		res = ast_streamfile(chan, "auth-thankyou", chan->language);
		if (!res)
			res = ast_waitstream(chan, "");
	} else {
		if (jump && ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101)) {
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
