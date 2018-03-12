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
 * \brief Provide a directory of extensions
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/
#include "asterisk.h"

#include <ctype.h>

#include "asterisk/paths.h" /* use ast_config_AST_SPOOL_DIR */
#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/say.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"

/*** DOCUMENTATION
	<application name="Directory" language="en_US">
		<synopsis>
			Provide directory of voicemail extensions.
		</synopsis>
		<syntax>
			<parameter name="vm-context">
				<para>This is the context within voicemail.conf to use for the Directory. If not
				specified and <literal>searchcontexts=no</literal> in
				<filename>voicemail.conf</filename>, then <literal>default</literal>
				will be assumed.</para>
			</parameter>
			<parameter name="dial-context" required="false">
				<para>This is the dialplan context to use when looking for an
				extension that the user has selected, or when jumping to the
				<literal>o</literal> or <literal>a</literal> extension. If not
				specified, the current context will be used.</para>
			</parameter>
			<parameter name="options" required="false">
				<optionlist>
					<option name="e">
						<para>In addition to the name, also read the extension number to the
						caller before presenting dialing options.</para>
					</option>
					<option name="f">
						<para>Allow the caller to enter the first name of a user in the
						directory instead of using the last name.  If specified, the
						optional number argument will be used for the number of
						characters the user should enter.</para>
						<argument name="n" required="true" />
					</option>
					<option name="l">
						<para>Allow the caller to enter the last name of a user in the
						directory.  This is the default.  If specified, the
						optional number argument will be used for the number of
						characters the user should enter.</para>
						<argument name="n" required="true" />
					</option>
					<option name="b">
						<para> Allow the caller to enter either the first or the last name
						of a user in the directory.  If specified, the optional number
						argument will be used for the number of characters the user should enter.</para>
						<argument name="n" required="true" />
					</option>
					<option name="a">
						<para>Allow the caller to additionally enter an alias for a user in the
						directory.  This option must be specified in addition to the
						<literal>f</literal>, <literal>l</literal>, or <literal>b</literal>
						option.</para>
					</option>
					<option name="m">
						<para>Instead of reading each name sequentially and asking for
						confirmation, create a menu of up to 8 names.</para>
					</option>
					<option name="n">
						<para>Read digits even if the channel is not answered.</para>
					</option>
					<option name="p">
						<para>Pause for n milliseconds after the digits are typed.  This is
						helpful for people with cellphones, who are not holding the
						receiver to their ear while entering DTMF.</para>
						<argument name="n" required="true" />
					</option>
				</optionlist>
				<note><para>Only one of the <replaceable>f</replaceable>, <replaceable>l</replaceable>, or <replaceable>b</replaceable>
				options may be specified. <emphasis>If more than one is specified</emphasis>, then Directory will act as
				if <replaceable>b</replaceable> was specified.  The number
				of characters for the user to type defaults to <literal>3</literal>.</para></note>

			</parameter>
		</syntax>
		<description>
			<para>This application will present the calling channel with a directory of extensions from which they can search
			by name. The list of names and corresponding extensions is retrieved from the
			voicemail configuration file, <filename>voicemail.conf</filename>.</para>
			<para>This application will immediately exit if one of the following DTMF digits are
			received and the extension to jump to exists:</para>
			<para><literal>0</literal> - Jump to the 'o' extension, if it exists.</para>
			<para><literal>*</literal> - Jump to the 'a' extension, if it exists.</para>
			<para>This application will set the following channel variable before completion:</para>
			<variablelist>
				<variable name="DIRECTORY_RESULT">
					<para>Reason Directory application exited.</para>
					<value name="OPERATOR">User requested operator</value>
					<value name="ASSISTANT">User requested assistant</value>
					<value name="TIMEOUT">User allowed DTMF wait duration to pass without sending DTMF</value>
					<value name="HANGUP">The channel hung up before the application finished</value>
					<value name="SELECTED">User selected a user to call from the directory</value>
					<value name="USEREXIT">User exited with '#' during selection</value>
					<value name="FAILED">The application failed</value>
				</variable>
			</variablelist>
		</description>
	</application>

 ***/
static const char app[] = "Directory";

/* For simplicity, I'm keeping the format compatible with the voicemail config,
   but i'm open to suggestions for isolating it */

#define VOICEMAIL_CONFIG "voicemail.conf"

enum {
	OPT_LISTBYFIRSTNAME = (1 << 0),
	OPT_SAYEXTENSION =    (1 << 1),
	OPT_FROMVOICEMAIL =   (1 << 2),
	OPT_SELECTFROMMENU =  (1 << 3),
	OPT_LISTBYLASTNAME =  (1 << 4),
	OPT_LISTBYEITHER =    OPT_LISTBYFIRSTNAME | OPT_LISTBYLASTNAME,
	OPT_PAUSE =           (1 << 5),
	OPT_NOANSWER =        (1 << 6),
	OPT_ALIAS =           (1 << 7),
};

enum {
	OPT_ARG_FIRSTNAME =   0,
	OPT_ARG_LASTNAME =    1,
	OPT_ARG_EITHER =      2,
	OPT_ARG_PAUSE =       3,
	/* This *must* be the last value in this enum! */
	OPT_ARG_ARRAY_SIZE =  4,
};

struct directory_item {
	char exten[AST_MAX_EXTENSION + 1];
	char name[AST_MAX_EXTENSION + 1];
	char context[AST_MAX_CONTEXT + 1];
	char key[50]; /* Text to order items. Either lastname+firstname or firstname+lastname */

	AST_LIST_ENTRY(directory_item) entry;
};

AST_APP_OPTIONS(directory_app_options, {
	AST_APP_OPTION_ARG('f', OPT_LISTBYFIRSTNAME, OPT_ARG_FIRSTNAME),
	AST_APP_OPTION_ARG('l', OPT_LISTBYLASTNAME, OPT_ARG_LASTNAME),
	AST_APP_OPTION_ARG('b', OPT_LISTBYEITHER, OPT_ARG_EITHER),
	AST_APP_OPTION_ARG('p', OPT_PAUSE, OPT_ARG_PAUSE),
	AST_APP_OPTION('e', OPT_SAYEXTENSION),
	AST_APP_OPTION('v', OPT_FROMVOICEMAIL),
	AST_APP_OPTION('m', OPT_SELECTFROMMENU),
	AST_APP_OPTION('n', OPT_NOANSWER),
	AST_APP_OPTION('a', OPT_ALIAS),
});

static int compare(const char *text, const char *template)
{
	char digit;

	if (ast_strlen_zero(text)) {
		return -1;
	}

	while (*template) {
		digit = toupper(*text++);
		switch (digit) {
		case 0:
			return -1;
		case '1':
			digit = '1';
			break;
		case '2':
		case 'A':
		case 'B':
		case 'C':
			digit = '2';
			break;
		case '3':
		case 'D':
		case 'E':
		case 'F':
			digit = '3';
			break;
		case '4':
		case 'G':
		case 'H':
		case 'I':
			digit = '4';
			break;
		case '5':
		case 'J':
		case 'K':
		case 'L':
			digit = '5';
			break;
		case '6':
		case 'M':
		case 'N':
		case 'O':
			digit = '6';
			break;
		case '7':
		case 'P':
		case 'Q':
		case 'R':
		case 'S':
			digit = '7';
			break;
		case '8':
		case 'T':
		case 'U':
		case 'V':
			digit = '8';
			break;
		case '9':
		case 'W':
		case 'X':
		case 'Y':
		case 'Z':
			digit = '9';
			break;

		default:
			if (digit > ' ')
				return -1;
			continue;
		}

		if (*template++ != digit)
			return -1;
	}

	return 0;
}

static int goto_exten(struct ast_channel *chan, const char *dialcontext, char *ext)
{
	if (!ast_goto_if_exists(chan, S_OR(dialcontext, ast_channel_context(chan)), ext, 1) ||
		(!ast_strlen_zero(ast_channel_macrocontext(chan)) &&
		!ast_goto_if_exists(chan, ast_channel_macrocontext(chan), ext, 1))) {
		return 0;
	} else {
		ast_log(LOG_WARNING, "Can't find extension '%s' in current context.  "
			"Not Exiting the Directory!\n", ext);
		return -1;
	}
}

/* play name of mailbox owner.
 * returns:  -1 for bad or missing extension
 *           '1' for selected entry from directory
 *           '*' for skipped entry from directory
 */
static int play_mailbox_owner(struct ast_channel *chan, const char *context,
	const char *ext, const char *name, struct ast_flags *flags)
{
	int res = 0;
	char *mailbox_id;

	mailbox_id = ast_alloca(strlen(ext) + strlen(context) + 2);
	sprintf(mailbox_id, "%s@%s", ext, context); /* Safe */

	res = ast_app_sayname(chan, mailbox_id);
	if (res >= 0) {
		ast_stopstream(chan);
		/* If Option 'e' was specified, also read the extension number with the name */
		if (ast_test_flag(flags, OPT_SAYEXTENSION)) {
			ast_stream_and_wait(chan, "vm-extension", AST_DIGIT_ANY);
			res = ast_say_character_str(chan, ext, AST_DIGIT_ANY, ast_channel_language(chan), AST_SAY_CASE_NONE);
		}
	} else {
		res = ast_say_character_str(chan, S_OR(name, ext), AST_DIGIT_ANY, ast_channel_language(chan), AST_SAY_CASE_NONE);
		if (!ast_strlen_zero(name) && ast_test_flag(flags, OPT_SAYEXTENSION)) {
			ast_stream_and_wait(chan, "vm-extension", AST_DIGIT_ANY);
			res = ast_say_character_str(chan, ext, AST_DIGIT_ANY, ast_channel_language(chan), AST_SAY_CASE_NONE);
		}
	}

	return res;
}

static int select_entry(struct ast_channel *chan, const char *dialcontext, const struct directory_item *item, struct ast_flags *flags)
{
	ast_debug(1, "Selecting '%s' - %s@%s\n", item->name, item->exten, S_OR(dialcontext, item->context));

	if (ast_test_flag(flags, OPT_FROMVOICEMAIL)) {
		/* We still want to set the exten though */
		ast_channel_exten_set(chan, item->exten);
	} else if (ast_goto_if_exists(chan, S_OR(dialcontext, item->context), item->exten, 1)) {
		ast_log(LOG_WARNING,
			"Can't find extension '%s' in context '%s'.  "
			"Did you pass the wrong context to Directory?\n",
			item->exten, S_OR(dialcontext, item->context));
		return -1;
	}

	pbx_builtin_setvar_helper(chan, "DIRECTORY_RESULT", "SELECTED");
	return 0;
}

static int select_item_pause(struct ast_channel *chan, struct ast_flags *flags, char *opts[])
{
	int res = 0, opt_pause = 0;

	if (ast_test_flag(flags, OPT_PAUSE) && !ast_strlen_zero(opts[OPT_ARG_PAUSE])) {
		opt_pause = atoi(opts[OPT_ARG_PAUSE]);
		if (opt_pause > 3000) {
			opt_pause = 3000;
		}
		res = ast_waitfordigit(chan, opt_pause);
	}
	return res;
}

static int select_item_seq(struct ast_channel *chan, struct directory_item **items, int count, const char *dialcontext, struct ast_flags *flags, char *opts[])
{
	struct directory_item *item, **ptr;
	int i, res, loop;

	/* option p(n): cellphone pause option */
	/* allow early press of selection key */
	res = select_item_pause(chan, flags, opts);

	for (ptr = items, i = 0; i < count; i++, ptr++) {
		item = *ptr;

		for (loop = 3 ; loop > 0; loop--) {
			if (!res)
				res = play_mailbox_owner(chan, item->context, item->exten, item->name, flags);
			if (!res)
				res = ast_stream_and_wait(chan, "dir-instr", AST_DIGIT_ANY);
			if (!res)
				res = ast_waitfordigit(chan, 3000);
			ast_stopstream(chan);

			if (res == '0') { /* operator selected */
				goto_exten(chan, dialcontext, "o");
				pbx_builtin_setvar_helper(chan, "DIRECTORY_RESULT", "OPERATOR");
				return '0';
			} else if (res == '1') { /* Name selected */
				return select_entry(chan, dialcontext, item, flags) ? -1 : 1;
			} else if (res == '*') {
				/* Skip to next match in list */
				break;
			} else if (res == '#') {
				/* Exit reading, continue in dialplan */
				pbx_builtin_setvar_helper(chan, "DIRECTORY_RESULT", "USEREXIT");
				return res;
			}

			if (res < 0)
				return -1;

			res = 0;
		}
		res = 0;
	}

	/* Nothing was selected */
	return 0;
}

static int select_item_menu(struct ast_channel *chan, struct directory_item **items, int count, const char *dialcontext, struct ast_flags *flags, char *opts[])
{
	struct directory_item **block, *item;
	int i, limit, res = 0;
	char buf[9];

	/* option p(n): cellphone pause option */
	select_item_pause(chan, flags, opts);

	for (block = items; count; block += limit, count -= limit) {
		limit = count;
		if (limit > 8)
			limit = 8;

		for (i = 0; i < limit && !res; i++) {
			item = block[i];

			snprintf(buf, sizeof(buf), "digits/%d", i + 1);
			/* Press <num> for <name>, [ extension <ext> ] */
			res = ast_streamfile(chan, "dir-multi1", ast_channel_language(chan));
			if (!res)
				res = ast_waitstream(chan, AST_DIGIT_ANY);
			if (!res)
				res = ast_streamfile(chan, buf, ast_channel_language(chan));
			if (!res)
				res = ast_waitstream(chan, AST_DIGIT_ANY);
			if (!res)
				res = ast_streamfile(chan, "dir-multi2", ast_channel_language(chan));
			if (!res)
				res = ast_waitstream(chan, AST_DIGIT_ANY);
			if (!res)
				res = play_mailbox_owner(chan, item->context, item->exten, item->name, flags);
			if (!res)
				res = ast_waitstream(chan, AST_DIGIT_ANY);
			if (!res)
				res = ast_waitfordigit(chan, 800);
		}

		/* Press "9" for more names. */
		if (!res && count > limit) {
			res = ast_streamfile(chan, "dir-multi9", ast_channel_language(chan));
			if (!res)
				res = ast_waitstream(chan, AST_DIGIT_ANY);
		}

		if (!res) {
			res = ast_waitfordigit(chan, 3000);
		}

		if (res && res > '0' && res < '1' + limit) {
			pbx_builtin_setvar_helper(chan, "DIRECTORY_RESULT", "SELECTED");
			return select_entry(chan, dialcontext, block[res - '1'], flags) ? -1 : 1;
		}

		if (res < 0)
			return -1;

		res = 0;
	}

	/* Nothing was selected */
	return 0;
}

AST_THREADSTORAGE(commonbuf);

static struct ast_config *realtime_directory(char *context)
{
	struct ast_config *cfg;
	struct ast_config *rtdata = NULL;
	struct ast_category *cat;
	struct ast_variable *var;
	char *category = NULL;
	const char *fullname;
	const char *hidefromdir, *searchcontexts = NULL;
	struct ast_flags config_flags = { 0 };
	struct ast_str *tmp = ast_str_thread_get(&commonbuf, 100);

	if (!tmp) {
		return NULL;
	}

	/* Load flat file config. */
	cfg = ast_config_load(VOICEMAIL_CONFIG, config_flags);

	if (!cfg) {
		/* Loading config failed. */
		ast_log(LOG_WARNING, "Loading config failed.\n");
		return NULL;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format.  Aborting.\n", VOICEMAIL_CONFIG);
		return NULL;
	}

	/* Get realtime entries, categorized by their mailbox number
	   and present in the requested context */
	if (ast_strlen_zero(context) && (searchcontexts = ast_variable_retrieve(cfg, "general", "searchcontexts"))) {
		if (ast_true(searchcontexts)) {
			rtdata = ast_load_realtime_multientry("voicemail", "mailbox LIKE", "%", SENTINEL);
			context = NULL;
		} else {
			rtdata = ast_load_realtime_multientry("voicemail", "mailbox LIKE", "%", "context", "default", SENTINEL);
			context = "default";
		}
	} else if (!ast_strlen_zero(context)) {
		rtdata = ast_load_realtime_multientry("voicemail", "mailbox LIKE", "%", "context", context, SENTINEL);
	}

	/* if there are no results, just return the entries from the config file */
	if (!rtdata) {
		return cfg;
	}

	while ((category = ast_category_browse(rtdata, category))) {
		const char *mailbox = ast_variable_retrieve(rtdata, category, "mailbox");
		const char *ctx = ast_variable_retrieve(rtdata, category, "context");

		if (ast_strlen_zero(mailbox)) {
			ast_debug(3, "Skipping result with missing or empty mailbox\n");
			continue;
		}

		fullname = ast_variable_retrieve(rtdata, category, "fullname");
		hidefromdir = ast_variable_retrieve(rtdata, category, "hidefromdir");
		if (ast_true(hidefromdir)) {
			/* Skip hidden */
			continue;
		}

		/* password,Full Name,email,pager,options */
		ast_str_set(&tmp, 0, "no-password,%s,,,", S_OR(fullname, ""));
		if (ast_variable_retrieve(rtdata, category, "alias")) {
			struct ast_variable *alias;
			for (alias = ast_variable_browse(rtdata, category); alias; alias = alias->next) {
				if (!strcasecmp(alias->name, "alias")) {
					ast_str_append(&tmp, 0, "|alias=%s", alias->value);
				}
			}
		}

		/* Does the context exist within the config file? If not, make one */
		if (!(cat = ast_category_get(cfg, ctx, NULL))) {
			if (!(cat = ast_category_new_dynamic(ctx))) {
				ast_log(LOG_WARNING, "Out of memory\n");
				ast_config_destroy(cfg);
				if (rtdata) {
					ast_config_destroy(rtdata);
				}
				return NULL;
			}
			ast_category_append(cfg, cat);
		}

		if ((var = ast_variable_new(mailbox, ast_str_buffer(tmp), ""))) {
			ast_variable_append(cat, var);
		} else {
			ast_log(LOG_WARNING, "Out of memory adding mailbox '%s'\n", mailbox);
		}
	}
	ast_config_destroy(rtdata);

	return cfg;
}

static int check_match(struct directory_item **result, const char *item_context, const char *item_fullname, const char *item_ext, const char *pattern_ext, int use_first_name)
{
	struct directory_item *item;
	const char *key = NULL;
	int namelen;

	if (ast_strlen_zero(item_fullname)) {
		return 0;
	}

	/* Set key to last name or first name depending on search mode */
	if (!use_first_name)
		key = strchr(item_fullname, ' ');

	if (key)
		key++;
	else
		key = item_fullname;

	if (compare(key, pattern_ext))
		return 0;

	ast_debug(1, "Found match %s@%s\n", item_ext, item_context);

	/* Match */
	item = ast_calloc(1, sizeof(*item));
	if (!item)
		return -1;
	ast_copy_string(item->context, item_context, sizeof(item->context));
	ast_copy_string(item->name, item_fullname, sizeof(item->name));
	ast_copy_string(item->exten, item_ext, sizeof(item->exten));

	ast_copy_string(item->key, key, sizeof(item->key));
	if (key != item_fullname) {
		/* Key is the last name. Append first name to key in order to sort Last,First */
		namelen = key - item_fullname - 1;
		if (namelen > sizeof(item->key) - strlen(item->key) - 1)
			namelen = sizeof(item->key) - strlen(item->key) - 1;
		strncat(item->key, item_fullname, namelen);
	}

	*result = item;
	return 1;
}

typedef AST_LIST_HEAD_NOLOCK(, directory_item) itemlist;

static int search_directory_sub(const char *context, struct ast_config *vmcfg, struct ast_config *ucfg, const char *ext, struct ast_flags flags, itemlist *alist)
{
	struct ast_variable *v;
	struct ast_str *buf = ast_str_thread_get(&commonbuf, 100);
	char *name;
	char *options;
	char *alias;
	char *cat;
	struct directory_item *item;
	int res;

	if (!buf) {
		return -1;
	}

	ast_debug(2, "Pattern: %s\n", ext);

	for (v = ast_variable_browse(vmcfg, context); v; v = v->next) {
		ast_str_set(&buf, 0, "%s", v->value);
		options = ast_str_buffer(buf);

		/* password,Full Name,email,pager,options */
		strsep(&options, ",");          /* Skip password */
		name = strsep(&options, ",");   /* Save full name */
		strsep(&options, ",");          /* Skip email */
		strsep(&options, ",");          /* Skip pager */
		/* options is now the options field if it exists. */

		if (options && strcasestr(options, "hidefromdir=yes")) {
			/* Ignore hidden */
			continue;
		}
		if (ast_strlen_zero(name)) {
			/* No name to compare against */
			continue;
		}

		res = 0;
		if (ast_test_flag(&flags, OPT_LISTBYLASTNAME)) {
			res = check_match(&item, context, name, v->name, ext, 0 /* use_first_name */);
		}
		if (!res && ast_test_flag(&flags, OPT_LISTBYFIRSTNAME)) {
			res = check_match(&item, context, name, v->name, ext, 1 /* use_first_name */);
		}
		if (!res && ast_test_flag(&flags, OPT_ALIAS)
			&& options && (alias = strcasestr(options, "alias="))) {
			char *a;

			ast_debug(1, "Found alias: %s\n", alias);
			while ((a = strsep(&alias, "|"))) {
				if (!strncasecmp(a, "alias=", 6)) {
					if ((res = check_match(&item, context, a + 6, v->name, ext, 1))) {
						break;
					}
				}
			}
		}

		if (!res) {
			continue;
		} else if (res < 0) {
			return -1;
		}

		AST_LIST_INSERT_TAIL(alist, item, entry);
	}

	if (ucfg) {
		for (cat = ast_category_browse(ucfg, NULL); cat ; cat = ast_category_browse(ucfg, cat)) {
			const char *position;

			if (!strcasecmp(cat, "general")) {
				continue;
			}
			if (!ast_true(ast_config_option(ucfg, cat, "hasdirectory"))) {
				continue;
			}

			/* Find all candidate extensions */
			if (!(position = ast_variable_retrieve(ucfg, cat, "fullname"))) {
				continue;
			}

			res = 0;
			if (ast_test_flag(&flags, OPT_LISTBYLASTNAME)) {
				res = check_match(&item, context, position, cat, ext, 0 /* use_first_name */);
			}
			if (!res && ast_test_flag(&flags, OPT_LISTBYFIRSTNAME)) {
				res = check_match(&item, context, position, cat, ext, 1 /* use_first_name */);
			}
			if (!res && ast_test_flag(&flags, OPT_ALIAS)) {
				for (v = ast_variable_browse(ucfg, cat); v; v = v->next) {
					if (!strcasecmp(v->name, "alias")
						&& (res = check_match(&item, context, v->value, cat, ext, 1))) {
						break;
					}
				}
			}

			if (!res) {
				continue;
			} else if (res < 0) {
				return -1;
			}

			AST_LIST_INSERT_TAIL(alist, item, entry);
		}
	}
	return 0;
}

static int search_directory(const char *context, struct ast_config *vmcfg, struct ast_config *ucfg, const char *ext, struct ast_flags flags, itemlist *alist)
{
	const char *searchcontexts = ast_variable_retrieve(vmcfg, "general", "searchcontexts");
	if (ast_strlen_zero(context)) {
		if (!ast_strlen_zero(searchcontexts) && ast_true(searchcontexts)) {
			/* Browse each context for a match */
			int res;
			const char *catg;
			for (catg = ast_category_browse(vmcfg, NULL); catg; catg = ast_category_browse(vmcfg, catg)) {
				if (!strcmp(catg, "general") || !strcmp(catg, "zonemessages")) {
					continue;
				}

				if ((res = search_directory_sub(catg, vmcfg, ucfg, ext, flags, alist))) {
					return res;
				}
			}
			return 0;
		} else {
			ast_debug(1, "Searching by category default\n");
			return search_directory_sub("default", vmcfg, ucfg, ext, flags, alist);
		}
	} else {
		/* Browse only the listed context for a match */
		ast_debug(1, "Searching by category %s\n", context);
		return search_directory_sub(context, vmcfg, ucfg, ext, flags, alist);
	}
}

static void sort_items(struct directory_item **sorted, int count)
{
	int reordered, i;
	struct directory_item **ptr, *tmp;

	if (count < 2)
		return;

	/* Bubble-sort items by the key */
	do {
		reordered = 0;
		for (ptr = sorted, i = 0; i < count - 1; i++, ptr++) {
			if (strcasecmp(ptr[0]->key, ptr[1]->key) > 0) {
				tmp = ptr[0];
				ptr[0] = ptr[1];
				ptr[1] = tmp;
				reordered++;
			}
		}
	} while (reordered);
}

static int do_directory(struct ast_channel *chan, struct ast_config *vmcfg, struct ast_config *ucfg, char *context, char *dialcontext, char digit, int digits, struct ast_flags *flags, char *opts[])
{
	/* Read in the first three digits..  "digit" is the first digit, already read */
	int res = 0;
	itemlist alist = AST_LIST_HEAD_NOLOCK_INIT_VALUE;
	struct directory_item *item, **ptr, **sorted = NULL;
	int count, i;
	char ext[10] = "";

	if (digit == '0' && !goto_exten(chan, dialcontext, "o")) {
		pbx_builtin_setvar_helper(chan, "DIRECTORY_RESULT", "OPERATOR");
		return digit;
	}

	if (digit == '*' && !goto_exten(chan, dialcontext, "a")) {
		pbx_builtin_setvar_helper(chan, "DIRECTORY_RESULT", "ASSISTANT");
		return digit;
	}

	ext[0] = digit;
	if (ast_readstring(chan, ext + 1, digits - 1, 3000, 3000, "#") < 0)
		return -1;

	res = search_directory(context, vmcfg, ucfg, ext, *flags, &alist);
	if (res)
		goto exit;

	/* Count items in the list */
	count = 0;
	AST_LIST_TRAVERSE(&alist, item, entry) {
		count++;
	}

	if (count < 1) {
		res = ast_streamfile(chan, "dir-nomatch", ast_channel_language(chan));
		goto exit;
	}


	/* Create plain array of pointers to items (for sorting) */
	sorted = ast_calloc(count, sizeof(*sorted));

	ptr = sorted;
	AST_LIST_TRAVERSE(&alist, item, entry) {
		*ptr++ = item;
	}

	/* Sort items */
	sort_items(sorted, count);

	if (DEBUG_ATLEAST(2)) {
		ast_log(LOG_DEBUG, "Listing matching entries:\n");
		for (ptr = sorted, i = 0; i < count; i++, ptr++) {
			ast_log(LOG_DEBUG, "%s: %s\n", ptr[0]->exten, ptr[0]->name);
		}
	}

	if (ast_test_flag(flags, OPT_SELECTFROMMENU)) {
		/* Offer multiple entries at the same time */
		res = select_item_menu(chan, sorted, count, dialcontext, flags, opts);
	} else {
		/* Offer entries one by one */
		res = select_item_seq(chan, sorted, count, dialcontext, flags, opts);
	}

	if (!res) {
		res = ast_streamfile(chan, "dir-nomore", ast_channel_language(chan));
	}

exit:
	if (sorted)
		ast_free(sorted);

	while ((item = AST_LIST_REMOVE_HEAD(&alist, entry)))
		ast_free(item);

	return res;
}

static int directory_exec(struct ast_channel *chan, const char *data)
{
	int res = 0, digit = 3;
	struct ast_config *cfg, *ucfg;
	const char *dirintro;
	char *parse, *opts[OPT_ARG_ARRAY_SIZE] = { 0, };
	struct ast_flags flags = { 0 };
	struct ast_flags config_flags = { 0 };
	enum { FIRST, LAST, BOTH } which = LAST;
	char digits[9] = "digits/3";
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(vmcontext);
		AST_APP_ARG(dialcontext);
		AST_APP_ARG(options);
	);

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.options && ast_app_parse_options(directory_app_options, &flags, opts, args.options))
		return -1;

	if (!(cfg = realtime_directory(args.vmcontext))) {
		ast_log(LOG_ERROR, "Unable to read the configuration data!\n");
		return -1;
	}

	if ((ucfg = ast_config_load("users.conf", config_flags)) == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file users.conf is in an invalid format.  Aborting.\n");
		ucfg = NULL;
	}

	dirintro = ast_variable_retrieve(cfg, args.vmcontext, "directoryintro");
	if (ast_strlen_zero(dirintro))
		dirintro = ast_variable_retrieve(cfg, "general", "directoryintro");
	/* the above prompts probably should be modified to include 0 for dialing operator
	   and # for exiting (continues in dialplan) */

	if (ast_test_flag(&flags, OPT_LISTBYFIRSTNAME) && ast_test_flag(&flags, OPT_LISTBYLASTNAME)) {
		if (!ast_strlen_zero(opts[OPT_ARG_EITHER])) {
			digit = atoi(opts[OPT_ARG_EITHER]);
		}
		which = BOTH;
	} else if (ast_test_flag(&flags, OPT_LISTBYFIRSTNAME)) {
		if (!ast_strlen_zero(opts[OPT_ARG_FIRSTNAME])) {
			digit = atoi(opts[OPT_ARG_FIRSTNAME]);
		}
		which = FIRST;
	} else {
		if (!ast_strlen_zero(opts[OPT_ARG_LASTNAME])) {
			digit = atoi(opts[OPT_ARG_LASTNAME]);
		}
		which = LAST;
	}

	/* If no options specified, search by last name */
	if (!ast_test_flag(&flags, OPT_LISTBYFIRSTNAME) && !ast_test_flag(&flags, OPT_LISTBYLASTNAME)) {
		ast_set_flag(&flags, OPT_LISTBYLASTNAME);
		which = LAST;
	}

	if (digit > 9) {
		digit = 9;
	} else if (digit < 1) {
		digit = 3;
	}
	digits[7] = digit + '0';

	if (ast_channel_state(chan) != AST_STATE_UP) {
		if (!ast_test_flag(&flags, OPT_NOANSWER)) {
			/* Otherwise answer unless we're supposed to read while on-hook */
			res = ast_answer(chan);
		}
	}
	for (;;) {
		if (!ast_strlen_zero(dirintro) && !res) {
			res = ast_stream_and_wait(chan, dirintro, AST_DIGIT_ANY);
		} else if (!res) {
			/* Stop playing sounds as soon as we have a digit. */
			res = ast_stream_and_wait(chan, "dir-welcome", AST_DIGIT_ANY);
			if (!res) {
				res = ast_stream_and_wait(chan, "dir-pls-enter", AST_DIGIT_ANY);
			}
			if (!res) {
				res = ast_stream_and_wait(chan, digits, AST_DIGIT_ANY);
			}
			if (!res) {
				res = ast_stream_and_wait(chan,
					which == FIRST ? "dir-first" :
					which == LAST ? "dir-last" :
					"dir-firstlast", AST_DIGIT_ANY);
			}
			if (!res) {
				res = ast_stream_and_wait(chan, "dir-usingkeypad", AST_DIGIT_ANY);
			}
		}
		ast_stopstream(chan);
		if (!res)
			res = ast_waitfordigit(chan, 5000);

		if (res <= 0) {
			if (res == 0) {
				pbx_builtin_setvar_helper(chan, "DIRECTORY_RESULT", "TIMEOUT");
			}
			break;
		}

		res = do_directory(chan, cfg, ucfg, args.vmcontext, args.dialcontext, res, digit, &flags, opts);
		if (res)
			break;

		res = ast_waitstream(chan, AST_DIGIT_ANY);
		ast_stopstream(chan);
		if (res < 0) {
			break;
		}
	}

	if (ucfg)
		ast_config_destroy(ucfg);
	ast_config_destroy(cfg);

	if (ast_check_hangup(chan)) {
		pbx_builtin_setvar_helper(chan, "DIRECTORY_RESULT", "HANGUP");
	} else if (res < 0) {
		/* If the res < 0 and we didn't hangup, an unaccounted for error must have happened. */
		pbx_builtin_setvar_helper(chan, "DIRECTORY_RESULT", "FAILED");
	}

	return res < 0 ? -1 : 0;
}

static int unload_module(void)
{
	int res;
	res = ast_unregister_application(app);
	return res;
}

static int load_module(void)
{
	return ast_register_application_xml(app, directory_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Extension Directory");
