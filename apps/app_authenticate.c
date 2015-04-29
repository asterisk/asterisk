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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/astdb.h"
#include "asterisk/utils.h"

enum {
	OPT_ACCOUNT = (1 << 0),
	OPT_DATABASE = (1 << 1),
	OPT_MULTIPLE = (1 << 3),
	OPT_REMOVE = (1 << 4),
};

AST_APP_OPTIONS(auth_app_options, {
	AST_APP_OPTION('a', OPT_ACCOUNT),
	AST_APP_OPTION('d', OPT_DATABASE),
	AST_APP_OPTION('m', OPT_MULTIPLE),
	AST_APP_OPTION('r', OPT_REMOVE),
});


static const char app[] = "Authenticate";
/*** DOCUMENTATION
	<application name="Authenticate" language="en_US">
		<synopsis>
			Authenticate a user
		</synopsis>
		<syntax>
			<parameter name="password" required="true">
				<para>Password the user should know</para>
			</parameter>
			<parameter name="options" required="false">
				<optionlist>
					<option name="a">
						<para>Set the channels' account code to the password that is entered</para>
					</option>
					<option name="d">
						<para>Interpret the given path as database key, not a literal file.</para>
						<note>
							<para>The value is not used at all in the authentication when using this option.
							If the family/key is set to <literal>/pin/100</literal> (value does not matter)
							then the password field needs to be set to <literal>/pin</literal> and the pin entered
							by the user would be authenticated against <literal>100</literal>.</para>
						</note>
					</option>
					<option name="m">
						<para>Interpret the given path as a file which contains a list of account
						codes and password hashes delimited with <literal>:</literal>, listed one per line in
						the file. When one of the passwords is matched, the channel will have
						its account code set to the corresponding account code in the file.</para>
					</option>
					<option name="r">
						<para>Remove the database key upon successful entry (valid with <literal>d</literal> only)</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="maxdigits" required="false">
				<para>maximum acceptable number of digits. Stops reading after
				maxdigits have been entered (without requiring the user to press the <literal>#</literal> key).
				Defaults to 0 - no limit - wait for the user press the <literal>#</literal> key.</para>
			</parameter>
			<parameter name="prompt" required="false">
				<para>Override the agent-pass prompt file.</para>
			</parameter>
		</syntax>
		<description>
			<para>This application asks the caller to enter a given password in order to continue dialplan execution.</para>
			<para>If the password begins with the <literal>/</literal> character, 
			it is interpreted as a file which contains a list of valid passwords, listed 1 password per line in the file.</para>
			<para>When using a database key, the value associated with the key can be anything.</para>
			<para>Users have three attempts to authenticate before the channel is hung up.</para>
		</description>
		<see-also>
			<ref type="application">VMAuthenticate</ref>
			<ref type="application">DISA</ref>
		</see-also>
	</application>
 ***/

static int auth_exec(struct ast_channel *chan, const char *data)
{
	int res = 0, retries, maxdigits;
	char passwd[256], *prompt = "agent-pass", *argcopy = NULL;
	struct ast_flags flags = {0};

	AST_DECLARE_APP_ARGS(arglist,
		AST_APP_ARG(password);
		AST_APP_ARG(options);
		AST_APP_ARG(maxdigits);
		AST_APP_ARG(prompt);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Authenticate requires an argument(password)\n");
		return -1;
	}

	if (ast_channel_state(chan) != AST_STATE_UP) {
		if ((res = ast_answer(chan)))
			return -1;
	}

	argcopy = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(arglist, argcopy);

	if (!ast_strlen_zero(arglist.options))
		ast_app_parse_options(auth_app_options, &flags, NULL, arglist.options);

	if (!ast_strlen_zero(arglist.maxdigits)) {
		maxdigits = atoi(arglist.maxdigits);
		if ((maxdigits<1) || (maxdigits>sizeof(passwd)-2))
			maxdigits = sizeof(passwd) - 2;
	} else {
		maxdigits = sizeof(passwd) - 2;
	}

	if (!ast_strlen_zero(arglist.prompt)) {
		prompt = arglist.prompt;
	} else {
		prompt = "agent-pass";
	}
   
	/* Start asking for password */
	for (retries = 0; retries < 3; retries++) {
		if ((res = ast_app_getdata(chan, prompt, passwd, maxdigits, 0)) < 0)
			break;

		res = 0;

		if (arglist.password[0] != '/') {
			/* Compare against a fixed password */
			if (!strcmp(passwd, arglist.password))
				break;
		} else if (ast_test_flag(&flags,OPT_DATABASE)) {
			char tmp[256];
			/* Compare against a database key */
			if (!ast_db_get(arglist.password + 1, passwd, tmp, sizeof(tmp))) {
				/* It's a good password */
				if (ast_test_flag(&flags,OPT_REMOVE))
					ast_db_del(arglist.password + 1, passwd);
				break;
			}
		} else {
			/* Compare against a file */
			FILE *f;
			char buf[256] = "", md5passwd[33] = "", *md5secret = NULL;

			if (!(f = fopen(arglist.password, "r"))) {
				ast_log(LOG_WARNING, "Unable to open file '%s' for authentication: %s\n", arglist.password, strerror(errno));
				continue;
			}

			for (;;) {
				size_t len;

				if (feof(f))
					break;

				if (!fgets(buf, sizeof(buf), f)) {
					continue;
				}

				if (ast_strlen_zero(buf))
					continue;

				len = strlen(buf) - 1;
				if (buf[len] == '\n')
					buf[len] = '\0';

				if (ast_test_flag(&flags, OPT_MULTIPLE)) {
					md5secret = buf;
					strsep(&md5secret, ":");
					if (!md5secret)
						continue;
					ast_md5_hash(md5passwd, passwd);
					if (!strcmp(md5passwd, md5secret)) {
						if (ast_test_flag(&flags, OPT_ACCOUNT)) {
							ast_channel_lock(chan);
							ast_channel_accountcode_set(chan, buf);
							ast_channel_unlock(chan);
						}
						break;
					}
				} else {
					if (!strcmp(passwd, buf)) {
						if (ast_test_flag(&flags, OPT_ACCOUNT)) {
							ast_channel_lock(chan);
							ast_channel_accountcode_set(chan, buf);
							ast_channel_unlock(chan);
						}
						break;
					}
				}
			}

			fclose(f);

			if (!ast_strlen_zero(buf)) {
				if (ast_test_flag(&flags, OPT_MULTIPLE)) {
					if (md5secret && !strcmp(md5passwd, md5secret))
						break;
				} else {
					if (!strcmp(passwd, buf))
						break;
				}
			}
		}
		prompt = "auth-incorrect";
	}

	if ((retries < 3) && !res) {
		if (ast_test_flag(&flags,OPT_ACCOUNT) && !ast_test_flag(&flags,OPT_MULTIPLE)) {
			ast_channel_lock(chan);
			ast_channel_accountcode_set(chan, passwd);
			ast_channel_unlock(chan);
		}
		if (!(res = ast_streamfile(chan, "auth-thankyou", ast_channel_language(chan))))
			res = ast_waitstream(chan, "");
	} else {
		if (!ast_streamfile(chan, "vm-goodbye", ast_channel_language(chan)))
			res = ast_waitstream(chan, "");
		res = -1;
	}

	return res;
}

static int load_module(void)
{
	if (ast_register_application_xml(app, auth_exec))
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Authentication Application");
