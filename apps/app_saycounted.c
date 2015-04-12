/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Trinity College Computing Center
 * Written by David Chappell
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
 * \brief Applications to decline words according to current language
 *
 * \author David Chappell <David.Chappell@trincoll.edu>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

/*** DOCUMENTATION
	<application name="SayCountedNoun" language="en_US">
		<synopsis>
			Say a noun in declined form in order to count things
		</synopsis>
		<syntax>
			<parameter name="number" required="true">
				<para>The number of things</para>
			</parameter>
			<parameter name="filename" required="true">
				<para>File name stem for the noun that is the the name of the things</para>
			</parameter>
		</syntax>
		<description>
			<para>Selects and plays the proper singular or plural form of a noun
			when saying things such as "five calls".  English has simple rules
			for deciding when to say "call" and when to say "calls", but other
			languages have complicated rules which would be extremely difficult
			to implement in the Asterisk dialplan language.</para>
			<para>The correct sound file is selected by examining the
			<replaceable>number</replaceable> and adding the appropriate suffix
			to <replaceable>filename</replaceable>. If the channel language is
			English, then the suffix will be either empty or "s". If the channel
			language is Russian or some other Slavic language, then the suffix
			will be empty for nominative, "x1" for genative singular, and "x2"
			for genative plural.</para>
			<para>Note that combining <replaceable>filename</replaceable> with
			a suffix will not necessarily produce a correctly spelled plural
			form. For example, SayCountedNoun(2,man) will play the sound file
			"mans" rather than "men". This behavior is intentional. Since the
			file name is never seen by the end user, there is no need to
			implement complicated spelling rules.  We simply record the word
			"men" in the sound file named "mans".</para>
			<para>This application does not automatically answer and should be
			preceeded by an application such as Answer() or Progress.</para>
		</description>
		<see-also>
			<ref type="application">SayCountedAdj</ref>
			<ref type="application">SayNumber</ref>
		</see-also>
	</application>
	<application name="SayCountedAdj" language="en_US">
		<synopsis>
			Say a adjective in declined form in order to count things
		</synopsis>
		<syntax>
			<parameter name="number" required="true">
				<para>The number of things</para>
			</parameter>
			<parameter name="filename" required="true">
				<para>File name stem for the adjective</para>
			</parameter>
			<parameter name="gender">
				<para>The gender of the noun modified, one of 'm', 'f', 'n', or 'c'</para>
			</parameter>
		</syntax>
		<description>
			<para>Selects and plays the proper form of an adjective according to
			the gender and of the noun which it modifies and the number of
			objects named by the noun-verb combination which have been counted.
			Used when saying things such as "5 new messages".  The various
			singular and plural forms of the adjective are selected by adding
			suffixes to <replaceable>filename</replaceable>.</para>
			<para>If the channel language is English, then no suffix will ever
			be added (since, in English, adjectives are not declined). If the
			channel language is Russian or some other slavic language, then the
			suffix will the specified <replaceable>gender</replaceable> for
			nominative, and "x" for genative plural. (The genative singular is
			not used when counting things.) For example, SayCountedAdj(1,new,f)
			will play sound file "newa" (containing the word "novaya"), but
			SayCountedAdj(5,new,f) will play sound file "newx" (containing the
			word "novikh").</para>
			<para>This application does not automatically answer and should be
			preceeded by an application such as Answer(), Progress(), or
			Proceeding().</para>
		</description>
		<see-also>
			<ref type="application">SayCountedNoun</ref>
			<ref type="application">SayNumber</ref>
		</see-also>
	</application>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/say.h"

static int saycountednoun_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	int number;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(number);
		AST_APP_ARG(noun);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SayCountedNoun requires two arguments (<number>,<noun>)\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc != 2) {
		ast_log(LOG_WARNING, "SayCountedNoun requires two arguments\n");
		return -1;
	}

	if (sscanf(args.number, "%d", &number) != 1) {
		ast_log(LOG_WARNING, "First argument must be a number between 0 and 2,147,483,647.\n");
		return -1;
	}

	return ast_say_counted_noun(chan, number, args.noun);
}

static int saycountedadj_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	int number;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(number);
		AST_APP_ARG(adjective);
		AST_APP_ARG(gender);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SayCountedAdj requires two or three arguments (<number>,<adjective>[,<gender>])\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc < 2) {
		ast_log(LOG_WARNING, "SayCountedAdj requires at least two arguments\n");
		return -1;
	}

	if (sscanf(args.number, "%d", &number) != 1) {
		ast_log(LOG_WARNING, "First argument must be a number between 0 and 2,147,483,647.\n");
		return -1;
	}

	if (!ast_strlen_zero(args.gender)) {
		if (strchr("cCfFmMnN", args.gender[0])) {
			ast_log(LOG_WARNING, "SayCountedAdj gender option must be one of 'f', 'm', 'c', or 'n'.\n");
			return -1;
		}
	}

	return ast_say_counted_adjective(chan, number, args.adjective, args.gender);
}

static int load_module(void)
{
	int res;
	res = ast_register_application_xml("SayCountedNoun", saycountednoun_exec);
	res |= ast_register_application_xml("SayCountedAdj", saycountedadj_exec);
	return res;
}

static int unload_module(void)
{
	int res;
	res = ast_unregister_application("SayCountedNoun");
	res |= ast_unregister_application("SayCountedAdj");
	return res;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Decline words according to channel language");

