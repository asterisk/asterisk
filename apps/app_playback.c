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
 * \brief Trivial application to playback a sound file
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/app.h"
#include "asterisk/cli.h"
#include "asterisk/localtime.h"
#include "asterisk/say.h"

static char *app = "Playback";

static char *synopsis = "Play a file";

static char *descrip = 
"  Playback(filename[&filename2...][|option]):  Plays back given filenames (do not put\n"
"extension). Options may also be included following a pipe symbol. The 'skip'\n"
"option causes the playback of the message to be skipped if the channel\n"
"is not in the 'up' state (i.e. it hasn't been  answered  yet). If 'skip' is \n"
"specified, the application will return immediately should the channel not be\n"
"off hook.  Otherwise, unless 'noanswer' is specified, the channel will\n"
"be answered before the sound is played. Not all channels support playing\n"
"messages while still on hook. If 'j' is specified, the application\n"
"will jump to priority n+101 if present when a file specified to be played\n"
"does not exist.\n"
"This application sets the following channel variable upon completion:\n"
" PLAYBACKSTATUS    The status of the playback attempt as a text string, one of\n"
"               SUCCESS | FAILED\n"
"See Also: Background (application) -- for playing soundfiles that are interruptible\n"
"          WaitExten (application) -- wait for digits from caller, optionally play music on hold\n"
;


static struct ast_config *say_cfg = NULL;
/* save the say' api calls.
 * The first entry is NULL if we have the standard source,
 * otherwise we are sourcing from here.
 * 'say load [new|old]' will enable the new or old method, or report status
 */
static const void * say_api_buf[40];
static const char *say_old = "old";
static const char *say_new = "new";

static void save_say_mode(const void *arg)
{
	int i = 0;
	say_api_buf[i++] = arg;

        say_api_buf[i++] = ast_say_number_full;
        say_api_buf[i++] = ast_say_enumeration_full;
        say_api_buf[i++] = ast_say_digit_str_full;
        say_api_buf[i++] = ast_say_character_str_full;
        say_api_buf[i++] = ast_say_phonetic_str_full;
        say_api_buf[i++] = ast_say_datetime;
        say_api_buf[i++] = ast_say_time;
        say_api_buf[i++] = ast_say_date;
        say_api_buf[i++] = ast_say_datetime_from_now;
        say_api_buf[i++] = ast_say_date_with_format;
}

static void restore_say_mode(void *arg)
{
	int i = 0;
	say_api_buf[i++] = arg;

        ast_say_number_full = say_api_buf[i++];
        ast_say_enumeration_full = say_api_buf[i++];
        ast_say_digit_str_full = say_api_buf[i++];
        ast_say_character_str_full = say_api_buf[i++];
        ast_say_phonetic_str_full = say_api_buf[i++];
        ast_say_datetime = say_api_buf[i++];
        ast_say_time = say_api_buf[i++];
        ast_say_date = say_api_buf[i++];
        ast_say_datetime_from_now = say_api_buf[i++];
        ast_say_date_with_format = say_api_buf[i++];
}

/* 
 * Typical 'say' arguments in addition to the date or number or string
 * to say. We do not include 'options' because they may be different
 * in recursive calls, and so they are better left as an external
 * parameter.
 */
typedef struct {
        struct ast_channel *chan;
        const char *ints;
        const char *language;
        int audiofd;
        int ctrlfd;
} say_args_t;

static int s_streamwait3(const say_args_t *a, const char *fn)
{
        int res = ast_streamfile(a->chan, fn, a->language);
        if (res) {
                ast_log(LOG_WARNING, "Unable to play message %s\n", fn);
                return res;
        }
        res = (a->audiofd  > -1 && a->ctrlfd > -1) ?
                ast_waitstream_full(a->chan, a->ints, a->audiofd, a->ctrlfd) :
                ast_waitstream(a->chan, a->ints);
        ast_stopstream(a->chan);
        return res;  
}

/*
 * the string is 'prefix:data' or prefix:fmt:data'
 * with ':' being invalid in strings.
 */
static int do_say(say_args_t *a, const char *s, const char *options, int depth)
{
	struct ast_variable *v;
	char *lang, *x, *rule = NULL;
	int ret = 0;   
	struct varshead head = { .first = NULL, .last = NULL };
	struct ast_var_t *n;

	if (depth++ > 10) {
		ast_log(LOG_WARNING, "recursion too deep, exiting\n");
		return -1;
	} else if (!say_cfg) {
		ast_log(LOG_WARNING, "no say.conf, cannot spell '%s'\n", s);
		return -1;
	}

	/* scan languages same as in file.c */
	if (a->language == NULL)
		a->language = "en";     /* default */
	lang = ast_strdupa(a->language);
	for (;;) {
		for (v = ast_variable_browse(say_cfg, lang); v ; v = v->next) {
			if (ast_extension_match(v->name, s)) {
				rule = ast_strdupa(v->value);
				break;
			}
		}
		if (rule)
			break;
		if ( (x = strchr(lang, '_')) )
			*x = '\0';      /* try without suffix */
		else if (strcmp(lang, "en"))
			lang = "en";    /* last resort, try 'en' if not done yet */
		else
			break;
	}
	if (!rule)
		return 0;

	/* skip up to two prefixes to get the value */
	if ( (x = strchr(s, ':')) )
		s = x + 1;
	if ( (x = strchr(s, ':')) )
		s = x + 1;
	n = ast_var_assign("SAY", s);
	AST_LIST_INSERT_HEAD(&head, n, entries);

	/* scan the body, one piece at a time */
	while ( !ret && (x = strsep(&rule, ",")) ) { /* exit on key */
		char fn[128];
		const char *p, *fmt, *data; /* format and data pointers */

		/* prepare a decent file name */
		x = ast_skip_blanks(x);
		ast_trim_blanks(x);

		/* replace variables */
		memset(fn, 0, sizeof(fn)); /* XXX why isn't done in pbx_substitute_variables_helper! */
		pbx_substitute_variables_varshead(&head, x, fn, sizeof(fn));

		/* locate prefix and data, if any */
		fmt = strchr(fn, ':');
		if (!fmt || fmt == fn)	{	/* regular filename */
			ret = s_streamwait3(a, fn);
			continue;
		}
		fmt++;
		data = strchr(fmt, ':');	/* colon before data */
		if (!data || data == fmt) {	/* simple prefix-fmt */
			ret = do_say(a, fn, options, depth);
			continue;
		}
		/* prefix:fmt:data */
		for (p = fmt; p < data && ret <= 0; p++) {
			char fn2[sizeof(fn)];
			if (*p == ' ' || *p == '\t')	/* skip blanks */
				continue;
			if (*p == '\'') {/* file name - we trim them */
				char *y;
				strcpy(fn2, ast_skip_blanks(p+1));	/* make a full copy */
				y = strchr(fn2, '\'');
				if (!y) {
					p = data;	/* invalid. prepare to end */
					break;
				}
				*y = '\0';
				ast_trim_blanks(fn2);
				p = strchr(p + 1, '\'');
				ret = s_streamwait3(a, fn2);
			} else {
				int l = fmt-fn;
				strcpy(fn2, fn); /* copy everything */
				/* after prefix, append the format */
				fn2[l++] = *p;
				strcpy(fn2 + l, data);
				ret = do_say(a, fn2, options, depth);
			}
			
			if (ret) {
				break;
			}
		}
	}
	ast_var_delete(n);
	return ret;
}

static int say_full(struct ast_channel *chan, const char *string,
        const char *ints, const char *lang, const char *options,
        int audiofd, int ctrlfd)
{
        say_args_t a = { chan, ints, lang, audiofd, ctrlfd };
        return do_say(&a, string, options, 0);
}

static int say_number_full(struct ast_channel *chan, int num,
	const char *ints, const char *lang, const char *options,
	int audiofd, int ctrlfd)
{
	char buf[64];
        say_args_t a = { chan, ints, lang, audiofd, ctrlfd };
	snprintf(buf, sizeof(buf), "num:%d", num);
        return do_say(&a, buf, options, 0);
}

static int say_enumeration_full(struct ast_channel *chan, int num,
	const char *ints, const char *lang, const char *options,
	int audiofd, int ctrlfd)
{
	char buf[64];
        say_args_t a = { chan, ints, lang, audiofd, ctrlfd };
	snprintf(buf, sizeof(buf), "enum:%d", num);
        return do_say(&a, buf, options, 0);
}

static int say_date_generic(struct ast_channel *chan, time_t t,
	const char *ints, const char *lang, const char *format, const char *timezone, const char *prefix)
{
	char buf[128];
	struct tm tm;
        say_args_t a = { chan, ints, lang, -1, -1 };
	if (format == NULL)
		format = "";

	ast_localtime(&t, &tm, NULL);
	snprintf(buf, sizeof(buf), "%s:%s:%04d%02d%02d%02d%02d.%02d-%d-%3d",
		prefix,
		format,
		tm.tm_year+1900,
		tm.tm_mon+1,
		tm.tm_mday,
		tm.tm_hour,
		tm.tm_min,
		tm.tm_sec,
		tm.tm_wday,
		tm.tm_yday);
        return do_say(&a, buf, NULL, 0);
}

static int say_date_with_format(struct ast_channel *chan, time_t t,
	const char *ints, const char *lang, const char *format, const char *timezone)
{
	return say_date_generic(chan, t, ints, lang, format, timezone, "datetime");
}

static int say_date(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	return say_date_generic(chan, t, ints, lang, "", NULL, "date");
}

static int say_time(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	return say_date_generic(chan, t, ints, lang, "", NULL, "time");
}

static int say_datetime(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	return say_date_generic(chan, t, ints, lang, "", NULL, "datetime");
}

/*
 * remap the 'say' functions to use those in this file
 */
static int __say_init(int fd, int argc, char *argv[])
{
	const char *old_mode = say_api_buf[0] ? say_new : say_old;
	char *mode;

	if (argc == 2) {
		ast_cli(fd, "say mode is [%s]\n", old_mode);
		return RESULT_SUCCESS;
        } else if (argc != 3)
                return RESULT_SHOWUSAGE;
        mode = argv[2];

	ast_log(LOG_WARNING, "init say.c from %s to %s\n", old_mode, mode);

	if (!strcmp(mode, old_mode)) {
		ast_log(LOG_WARNING, "say mode is %s already\n", mode);
	} else if (!strcmp(mode, say_new)) {
		if (say_cfg == NULL)
			say_cfg = ast_config_load("say.conf");
		save_say_mode(say_new);
		ast_say_number_full = say_number_full;

		ast_say_enumeration_full = say_enumeration_full;
#if 0
		ast_say_digits_full = say_digits_full;
		ast_say_digit_str_full = say_digit_str_full;
		ast_say_character_str_full = say_character_str_full;
		ast_say_phonetic_str_full = say_phonetic_str_full;
		ast_say_datetime_from_now = say_datetime_from_now;
#endif
		ast_say_datetime = say_datetime;
		ast_say_time = say_time;
		ast_say_date = say_date;
		ast_say_date_with_format = say_date_with_format;
	} else if (!strcmp(mode, say_old) && say_api_buf[0] == say_new) {
		restore_say_mode(NULL);
	} else {
		ast_log(LOG_WARNING, "unrecognized mode %s\n", mode);
	}
	return RESULT_SUCCESS;
}

static struct ast_cli_entry cli_playback[] = {
        { { "say", "load", NULL },
	__say_init, "set/show the say mode",
	"Usage: say load [new|old]\n    Set/show the say mode\n" },
};

static int playback_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	int mres = 0;
	struct ast_module_user *u;
	char *tmp;
	int option_skip=0;
	int option_say=0;
	int option_noanswer = 0;
	int priority_jump = 0;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filenames);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Playback requires an argument (filename)\n");
		return -1;
	}

	tmp = ast_strdupa(data);

	u = ast_module_user_add(chan);
	AST_STANDARD_APP_ARGS(args, tmp);

	if (args.options) {
		if (strcasestr(args.options, "skip"))
			option_skip = 1;
		if (strcasestr(args.options, "say"))
			option_say = 1;
		if (strcasestr(args.options, "noanswer"))
			option_noanswer = 1;
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}
	
	if (chan->_state != AST_STATE_UP) {
		if (option_skip) {
			/* At the user's option, skip if the line is not up */
			goto done;
		} else if (!option_noanswer)
			/* Otherwise answer unless we're supposed to send this while on-hook */
			res = ast_answer(chan);
	}
	if (!res) {
		char *back = args.filenames;
		char *front;

		ast_stopstream(chan);
		while (!res && (front = strsep(&back, "&"))) {
			if (option_say)
				res = say_full(chan, front, "", chan->language, NULL, -1, -1);
			else
				res = ast_streamfile(chan, front, chan->language);
			if (!res) { 
				res = ast_waitstream(chan, "");	
				ast_stopstream(chan);
			} else {
				ast_log(LOG_WARNING, "ast_streamfile failed on %s for %s\n", chan->name, (char *)data);
				if (priority_jump || ast_opt_priority_jumping)
					ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
				res = 0;
				mres = 1;
			}
		}
	}
done:
	pbx_builtin_setvar_helper(chan, "PLAYBACKSTATUS", mres ? "FAILED" : "SUCCESS");
	ast_module_user_remove(u);
	return res;
}

static int reload(void)
{
	if (say_cfg) {
		ast_config_destroy(say_cfg);
		ast_log(LOG_NOTICE, "Reloading say.conf\n");
	}
	say_cfg = ast_config_load("say.conf");
	/*
	 * XXX here we should sort rules according to the same order
	 * we have in pbx.c so we have the same matching behaviour.
	 */
	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	ast_cli_unregister_multiple(cli_playback, sizeof(cli_playback) / sizeof(struct ast_cli_entry));

	ast_module_user_hangup_all();

	if (say_cfg)
		ast_config_destroy(say_cfg);

	return res;	
}

static int load_module(void)
{
	say_cfg = ast_config_load("say.conf");
        ast_cli_register_multiple(cli_playback, sizeof(cli_playback) / sizeof(struct ast_cli_entry));
	return ast_register_application(app, playback_exec, synopsis, descrip);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Sound File Playback Application",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
