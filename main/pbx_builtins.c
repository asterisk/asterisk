/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015 Fairview 5 Engineering, LLC
 *
 * George Joseph <george.joseph@fairview5.com>
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
 * \brief Core PBX builtin routines.
 *
 * \author George Joseph <george.joseph@fairview5.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/pbx.h"
#include "asterisk/causes.h"
#include "asterisk/indications.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/say.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "pbx_private.h"

#define BACKGROUND_SKIP		(1 << 0)
#define BACKGROUND_NOANSWER	(1 << 1)
#define BACKGROUND_MATCHEXTEN	(1 << 2)
#define BACKGROUND_PLAYBACK	(1 << 3)

AST_APP_OPTIONS(background_opts, {
	AST_APP_OPTION('s', BACKGROUND_SKIP),
	AST_APP_OPTION('n', BACKGROUND_NOANSWER),
	AST_APP_OPTION('m', BACKGROUND_MATCHEXTEN),
	AST_APP_OPTION('p', BACKGROUND_PLAYBACK),
});

#define WAITEXTEN_MOH		(1 << 0)
#define WAITEXTEN_DIALTONE	(1 << 1)

AST_APP_OPTIONS(waitexten_opts, {
	AST_APP_OPTION_ARG('m', WAITEXTEN_MOH, 0),
	AST_APP_OPTION_ARG('d', WAITEXTEN_DIALTONE, 0),
});

static ast_rwlock_t *globalslock;
static struct varshead *globals;

static int pbx_builtin_answer(struct ast_channel *, const char *);
static int pbx_builtin_goto(struct ast_channel *, const char *);
static int pbx_builtin_hangup(struct ast_channel *, const char *);
static int pbx_builtin_background(struct ast_channel *, const char *);
static int pbx_builtin_wait(struct ast_channel *, const char *);
static int pbx_builtin_waitexten(struct ast_channel *, const char *);
static int pbx_builtin_incomplete(struct ast_channel *, const char *);
static int pbx_builtin_setamaflags(struct ast_channel *, const char *);
static int pbx_builtin_ringing(struct ast_channel *, const char *);
static int pbx_builtin_proceeding(struct ast_channel *, const char *);
static int pbx_builtin_progress(struct ast_channel *, const char *);
static int pbx_builtin_noop(struct ast_channel *, const char *);
static int pbx_builtin_gotoif(struct ast_channel *, const char *);
static int pbx_builtin_gotoiftime(struct ast_channel *, const char *);
static int pbx_builtin_execiftime(struct ast_channel *, const char *);
static int pbx_builtin_saynumber(struct ast_channel *, const char *);
static int pbx_builtin_saydigits(struct ast_channel *, const char *);
static int pbx_builtin_saycharacters(struct ast_channel *, const char *);
static int pbx_builtin_saycharacters_case(struct ast_channel *, const char *);
static int pbx_builtin_sayphonetic(struct ast_channel *, const char *);
static int pbx_builtin_importvar(struct ast_channel *, const char *);

/*! \brief Declaration of builtin applications */
struct pbx_builtin {
	char name[AST_MAX_APP];
	int (*execute)(struct ast_channel *chan, const char *data);
} builtins[] =
{
	/* These applications are built into the PBX core and do not
	   need separate modules */

	{ "Answer",         pbx_builtin_answer },
	{ "BackGround",     pbx_builtin_background },
	{ "Busy",           pbx_builtin_busy },
	{ "Congestion",     pbx_builtin_congestion },
	{ "ExecIfTime",     pbx_builtin_execiftime },
	{ "Goto",           pbx_builtin_goto },
	{ "GotoIf",         pbx_builtin_gotoif },
	{ "GotoIfTime",     pbx_builtin_gotoiftime },
	{ "ImportVar",      pbx_builtin_importvar },
	{ "Hangup",         pbx_builtin_hangup },
	{ "Incomplete",     pbx_builtin_incomplete },
	{ "NoOp",           pbx_builtin_noop },
	{ "Proceeding",     pbx_builtin_proceeding },
	{ "Progress",       pbx_builtin_progress },
	{ "RaiseException", pbx_builtin_raise_exception },
	{ "Ringing",        pbx_builtin_ringing },
	{ "SayAlpha",       pbx_builtin_saycharacters },
	{ "SayAlphaCase",   pbx_builtin_saycharacters_case },
	{ "SayDigits",      pbx_builtin_saydigits },
	{ "SayNumber",      pbx_builtin_saynumber },
	{ "SayPhonetic",    pbx_builtin_sayphonetic },
	{ "Set",            pbx_builtin_setvar },
	{ "MSet",           pbx_builtin_setvar_multiple },
	{ "SetAMAFlags",    pbx_builtin_setamaflags },
	{ "Wait",           pbx_builtin_wait },
	{ "WaitExten",      pbx_builtin_waitexten }
};

int pbx_builtin_raise_exception(struct ast_channel *chan, const char *reason)
{
	/* Priority will become 1, next time through the AUTOLOOP */
	return raise_exception(chan, reason, 0);
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_proceeding(struct ast_channel *chan, const char *data)
{
	ast_indicate(chan, AST_CONTROL_PROCEEDING);
	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_progress(struct ast_channel *chan, const char *data)
{
	ast_indicate(chan, AST_CONTROL_PROGRESS);
	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_ringing(struct ast_channel *chan, const char *data)
{
	ast_indicate(chan, AST_CONTROL_RINGING);
	return 0;
}

/*!
 * \ingroup applications
 */
int pbx_builtin_busy(struct ast_channel *chan, const char *data)
{
	ast_indicate(chan, AST_CONTROL_BUSY);
	/* Don't change state of an UP channel, just indicate
	   busy in audio */
	ast_channel_lock(chan);
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_channel_hangupcause_set(chan, AST_CAUSE_BUSY);
		ast_setstate(chan, AST_STATE_BUSY);
	}
	ast_channel_unlock(chan);
	wait_for_hangup(chan, data);
	return -1;
}

/*!
 * \ingroup applications
 */
int pbx_builtin_congestion(struct ast_channel *chan, const char *data)
{
	ast_indicate(chan, AST_CONTROL_CONGESTION);
	/* Don't change state of an UP channel, just indicate
	   congestion in audio */
	ast_channel_lock(chan);
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_channel_hangupcause_set(chan, AST_CAUSE_CONGESTION);
		ast_setstate(chan, AST_STATE_BUSY);
	}
	ast_channel_unlock(chan);
	wait_for_hangup(chan, data);
	return -1;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_answer(struct ast_channel *chan, const char *data)
{
	int delay = 0;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(delay);
		AST_APP_ARG(answer_cdr);
	);

	if (ast_strlen_zero(data)) {
		return __ast_answer(chan, 0);
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.delay) && (ast_channel_state(chan) != AST_STATE_UP))
		delay = atoi(data);

	if (delay < 0) {
		delay = 0;
	}

	if (!ast_strlen_zero(args.answer_cdr) && !strcasecmp(args.answer_cdr, "nocdr")) {
		ast_log(AST_LOG_WARNING, "The nocdr option for the Answer application has been removed and is no longer supported.\n");
	}

	return __ast_answer(chan, delay);
}

static int pbx_builtin_incomplete(struct ast_channel *chan, const char *data)
{
	const char *options = data;
	int answer = 1;

	/* Some channels can receive DTMF in unanswered state; some cannot */
	if (!ast_strlen_zero(options) && strchr(options, 'n')) {
		answer = 0;
	}

	/* If the channel is hungup, stop waiting */
	if (ast_check_hangup(chan)) {
		return -1;
	} else if (ast_channel_state(chan) != AST_STATE_UP && answer) {
		__ast_answer(chan, 0);
	}

	ast_indicate(chan, AST_CONTROL_INCOMPLETE);

	return AST_PBX_INCOMPLETE;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_setamaflags(struct ast_channel *chan, const char *data)
{
	ast_log(AST_LOG_WARNING, "The SetAMAFlags application is deprecated. Please use the CHANNEL function instead.\n");

	if (ast_strlen_zero(data)) {
		ast_log(AST_LOG_WARNING, "No parameter passed to SetAMAFlags\n");
		return 0;
	}
	/* Copy the AMA Flags as specified */
	ast_channel_lock(chan);
	if (isdigit(data[0])) {
		int amaflags;
		if (sscanf(data, "%30d", &amaflags) != 1) {
			ast_log(AST_LOG_WARNING, "Unable to set AMA flags on channel %s\n", ast_channel_name(chan));
			ast_channel_unlock(chan);
			return 0;
		}
		ast_channel_amaflags_set(chan, amaflags);
	} else {
		ast_channel_amaflags_set(chan, ast_channel_string2amaflag(data));
	}
	ast_channel_unlock(chan);
	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_hangup(struct ast_channel *chan, const char *data)
{
	int cause;

	ast_set_hangupsource(chan, "dialplan/builtin", 0);

	if (!ast_strlen_zero(data)) {
		cause = ast_str2cause(data);
		if (cause <= 0) {
			if (sscanf(data, "%30d", &cause) != 1 || cause <= 0) {
				ast_log(LOG_WARNING, "Invalid cause given to Hangup(): \"%s\"\n", data);
				cause = 0;
			}
		}
	} else {
		cause = 0;
	}

	ast_channel_lock(chan);
	if (cause <= 0) {
		cause = ast_channel_hangupcause(chan);
		if (cause <= 0) {
			cause = AST_CAUSE_NORMAL_CLEARING;
		}
	}
	ast_channel_hangupcause_set(chan, cause);
	ast_softhangup_nolock(chan, AST_SOFTHANGUP_EXPLICIT);
	ast_channel_unlock(chan);

	return -1;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_gotoiftime(struct ast_channel *chan, const char *data)
{
	char *s, *ts, *branch1, *branch2, *branch;
	struct ast_timing timing;
	const char *ctime;
	struct timeval tv = ast_tvnow();
	long timesecs;

	if (!chan) {
		ast_log(LOG_WARNING, "GotoIfTime requires a channel on which to operate\n");
		return -1;
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "GotoIfTime requires an argument:\n  <time range>,<days of week>,<days of month>,<months>[,<timezone>]?'labeliftrue':'labeliffalse'\n");
		return -1;
	}

	ts = s = ast_strdupa(data);

	ast_channel_lock(chan);
	if ((ctime = pbx_builtin_getvar_helper(chan, "TESTTIME")) && sscanf(ctime, "%ld", &timesecs) == 1) {
		tv.tv_sec = timesecs;
	} else if (ctime) {
		ast_log(LOG_WARNING, "Using current time to evaluate\n");
		/* Reset when unparseable */
		pbx_builtin_setvar_helper(chan, "TESTTIME", NULL);
	}
	ast_channel_unlock(chan);

	/* Separate the Goto path */
	strsep(&ts, "?");
	branch1 = strsep(&ts,":");
	branch2 = strsep(&ts,"");

	/* struct ast_include include contained garbage here, fixed by zeroing it on get_timerange */
	if (ast_build_timing(&timing, s) && ast_check_timing2(&timing, tv)) {
		branch = branch1;
	} else {
		branch = branch2;
	}
	ast_destroy_timing(&timing);

	if (ast_strlen_zero(branch)) {
		ast_debug(1, "Not taking any branch\n");
		return 0;
	}

	return pbx_builtin_goto(chan, branch);
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_execiftime(struct ast_channel *chan, const char *data)
{
	char *s, *appname;
	struct ast_timing timing;
	struct ast_app *app;
	static const char * const usage = "ExecIfTime requires an argument:\n  <time range>,<days of week>,<days of month>,<months>[,<timezone>]?<appname>[(<appargs>)]";

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s\n", usage);
		return -1;
	}

	appname = ast_strdupa(data);

	s = strsep(&appname, "?");	/* Separate the timerange and application name/data */
	if (!appname) {	/* missing application */
		ast_log(LOG_WARNING, "%s\n", usage);
		return -1;
	}

	if (!ast_build_timing(&timing, s)) {
		ast_log(LOG_WARNING, "Invalid Time Spec: %s\nCorrect usage: %s\n", s, usage);
		ast_destroy_timing(&timing);
		return -1;
	}

	if (!ast_check_timing(&timing))	{ /* outside the valid time window, just return */
		ast_destroy_timing(&timing);
		return 0;
	}
	ast_destroy_timing(&timing);

	/* now split appname(appargs) */
	if ((s = strchr(appname, '('))) {
		char *e;
		*s++ = '\0';
		if ((e = strrchr(s, ')')))
			*e = '\0';
		else
			ast_log(LOG_WARNING, "Failed to find closing parenthesis\n");
	}


	if ((app = pbx_findapp(appname))) {
		return pbx_exec(chan, app, S_OR(s, ""));
	} else {
		ast_log(LOG_WARNING, "Cannot locate application %s\n", appname);
		return -1;
	}
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_wait(struct ast_channel *chan, const char *data)
{
	int ms;

	/* Wait for "n" seconds */
	if (!ast_app_parse_timelen(data, &ms, TIMELEN_SECONDS) && ms > 0) {
		return ast_safe_sleep(chan, ms);
	}
	return 0;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_waitexten(struct ast_channel *chan, const char *data)
{
	int ms, res;
	struct ast_flags flags = {0};
	char *opts[1] = { NULL };
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(timeout);
		AST_APP_ARG(options);
	);

	if (!ast_strlen_zero(data)) {
		parse = ast_strdupa(data);
		AST_STANDARD_APP_ARGS(args, parse);
	} else
		memset(&args, 0, sizeof(args));

	if (args.options)
		ast_app_parse_options(waitexten_opts, &flags, opts, args.options);

	if (ast_test_flag(&flags, WAITEXTEN_MOH) && !opts[0] ) {
		ast_log(LOG_WARNING, "The 'm' option has been specified for WaitExten without a class.\n");
	} else if (ast_test_flag(&flags, WAITEXTEN_MOH)) {
		ast_indicate_data(chan, AST_CONTROL_HOLD, S_OR(opts[0], NULL),
			!ast_strlen_zero(opts[0]) ? strlen(opts[0]) + 1 : 0);
	} else if (ast_test_flag(&flags, WAITEXTEN_DIALTONE)) {
		struct ast_tone_zone_sound *ts = ast_get_indication_tone(ast_channel_zone(chan), "dial");
		if (ts) {
			ast_playtones_start(chan, 0, ts->data, 0);
			ts = ast_tone_zone_sound_unref(ts);
		} else {
			ast_tonepair_start(chan, 350, 440, 0, 0);
		}
	}
	/* Wait for "n" seconds */
	if (!ast_app_parse_timelen(args.timeout, &ms, TIMELEN_SECONDS) && ms > 0) {
		/* Yay! */
	} else if (ast_channel_pbx(chan)) {
		ms = ast_channel_pbx(chan)->rtimeoutms;
	} else {
		ms = 10000;
	}

	res = ast_waitfordigit(chan, ms);
	if (!res) {
		if (ast_check_hangup(chan)) {
			/* Call is hungup for some reason. */
			res = -1;
		} else if (ast_exists_extension(chan, ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan) + 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
			ast_verb(3, "Timeout on %s, continuing...\n", ast_channel_name(chan));
		} else if (ast_exists_extension(chan, ast_channel_context(chan), "t", 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
			ast_verb(3, "Timeout on %s, going to 't'\n", ast_channel_name(chan));
			set_ext_pri(chan, "t", 0); /* 0 will become 1, next time through the loop */
		} else if (ast_exists_extension(chan, ast_channel_context(chan), "e", 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
			raise_exception(chan, "RESPONSETIMEOUT", 0); /* 0 will become 1, next time through the loop */
		} else {
			ast_log(LOG_WARNING, "Timeout but no rule 't' or 'e' in context '%s'\n",
				ast_channel_context(chan));
			res = -1;
		}
	}

	if (ast_test_flag(&flags, WAITEXTEN_MOH))
		ast_indicate(chan, AST_CONTROL_UNHOLD);
	else if (ast_test_flag(&flags, WAITEXTEN_DIALTONE))
		ast_playtones_stop(chan);

	return res;
}

/*!
 * \ingroup applications
 */
static int pbx_builtin_background(struct ast_channel *chan, const char *data)
{
	int res = 0;
	int mres = 0;
	struct ast_flags flags = {0};
	char *parse, exten[2] = "";
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(options);
		AST_APP_ARG(lang);
		AST_APP_ARG(context);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Background requires an argument (filename)\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.lang))
		args.lang = (char *)ast_channel_language(chan);	/* XXX this is const */

	if (ast_strlen_zero(args.context)) {
		const char *context;
		ast_channel_lock(chan);
		if ((context = pbx_builtin_getvar_helper(chan, "MACRO_CONTEXT"))) {
			args.context = ast_strdupa(context);
		} else {
			args.context = ast_strdupa(ast_channel_context(chan));
		}
		ast_channel_unlock(chan);
	}

	if (args.options) {
		if (!strcasecmp(args.options, "skip"))
			flags.flags = BACKGROUND_SKIP;
		else if (!strcasecmp(args.options, "noanswer"))
			flags.flags = BACKGROUND_NOANSWER;
		else
			ast_app_parse_options(background_opts, &flags, NULL, args.options);
	}

	/* Answer if need be */
	if (ast_channel_state(chan) != AST_STATE_UP) {
		if (ast_test_flag(&flags, BACKGROUND_SKIP)) {
			goto done;
		} else if (!ast_test_flag(&flags, BACKGROUND_NOANSWER)) {
			res = ast_answer(chan);
		}
	}

	if (!res) {
		char *back = ast_strip(args.filename);
		char *front;

		ast_stopstream(chan);		/* Stop anything playing */
		/* Stream the list of files */
		while (!res && (front = strsep(&back, "&")) ) {
			if ( (res = ast_streamfile(chan, front, args.lang)) ) {
				ast_log(LOG_WARNING, "ast_streamfile failed on %s for %s\n", ast_channel_name(chan), (char*)data);
				res = 0;
				mres = 1;
				break;
			}
			if (ast_test_flag(&flags, BACKGROUND_PLAYBACK)) {
				res = ast_waitstream(chan, "");
			} else if (ast_test_flag(&flags, BACKGROUND_MATCHEXTEN)) {
				res = ast_waitstream_exten(chan, args.context);
			} else {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
			}
			ast_stopstream(chan);
		}
	}

	/*
	 * If the single digit DTMF is an extension in the specified context, then
	 * go there and signal no DTMF.  Otherwise, we should exit with that DTMF.
	 * If we're in Macro, we'll exit and seek that DTMF as the beginning of an
	 * extension in the Macro's calling context.  If we're not in Macro, then
	 * we'll simply seek that extension in the calling context.  Previously,
	 * someone complained about the behavior as it related to the interior of a
	 * Gosub routine, and the fix (#14011) inadvertently broke FreePBX
	 * (#14940).  This change should fix both of these situations, but with the
	 * possible incompatibility that if a single digit extension does not exist
	 * (but a longer extension COULD have matched), it would have previously
	 * gone immediately to the "i" extension, but will now need to wait for a
	 * timeout.
	 *
	 * Later, we had to add a flag to disable this workaround, because AGI
	 * users can EXEC Background and reasonably expect that the DTMF code will
	 * be returned (see #16434).
	 */
	if (!ast_test_flag(ast_channel_flags(chan), AST_FLAG_DISABLE_WORKAROUNDS)
		&& (exten[0] = res)
		&& ast_canmatch_extension(chan, args.context, exten, 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))
		&& !ast_matchmore_extension(chan, args.context, exten, 1,
			S_COR(ast_channel_caller(chan)->id.number.valid, ast_channel_caller(chan)->id.number.str, NULL))) {
		char buf[2] = { 0, };
		snprintf(buf, sizeof(buf), "%c", res);
		ast_channel_exten_set(chan, buf);
		ast_channel_context_set(chan, args.context);
		ast_channel_priority_set(chan, 0);
		res = 0;
	}
done:
	pbx_builtin_setvar_helper(chan, "BACKGROUNDSTATUS", mres ? "FAILED" : "SUCCESS");
	return res;
}

/*! Goto
 * \ingroup applications
 */
static int pbx_builtin_goto(struct ast_channel *chan, const char *data)
{
	int res = ast_parseable_goto(chan, data);
	if (!res)
		ast_verb(3, "Goto (%s,%s,%d)\n", ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan) + 1);
	return res;
}

int pbx_builtin_serialize_variables(struct ast_channel *chan, struct ast_str **buf)
{
	struct ast_var_t *variables;
	const char *var, *val;
	int total = 0;

	if (!chan)
		return 0;

	ast_str_reset(*buf);

	ast_channel_lock(chan);

	AST_LIST_TRAVERSE(ast_channel_varshead(chan), variables, entries) {
		if ((var = ast_var_name(variables)) && (val = ast_var_value(variables))
		   /* && !ast_strlen_zero(var) && !ast_strlen_zero(val) */
		   ) {
			if (ast_str_append(buf, 0, "%s=%s\n", var, val) < 0) {
				ast_log(LOG_ERROR, "Data Buffer Size Exceeded!\n");
				break;
			} else
				total++;
		} else
			break;
	}

	ast_channel_unlock(chan);

	return total;
}

const char *pbx_builtin_getvar_helper(struct ast_channel *chan, const char *name)
{
	struct ast_var_t *variables;
	const char *ret = NULL;
	int i;
	struct varshead *places[2] = { NULL, globals };

	if (!name)
		return NULL;

	if (chan) {
		ast_channel_lock(chan);
		places[0] = ast_channel_varshead(chan);
	}

	for (i = 0; i < 2; i++) {
		if (!places[i])
			continue;
		if (places[i] == globals)
			ast_rwlock_rdlock(globalslock);
		AST_LIST_TRAVERSE(places[i], variables, entries) {
			if (!strcmp(name, ast_var_name(variables))) {
				ret = ast_var_value(variables);
				break;
			}
		}
		if (places[i] == globals)
			ast_rwlock_unlock(globalslock);
		if (ret)
			break;
	}

	if (chan)
		ast_channel_unlock(chan);

	return ret;
}

void pbx_builtin_pushvar_helper(struct ast_channel *chan, const char *name, const char *value)
{
	struct ast_var_t *newvariable;
	struct varshead *headp;

	if (name[strlen(name)-1] == ')') {
		char *function = ast_strdupa(name);

		ast_log(LOG_WARNING, "Cannot push a value onto a function\n");
		ast_func_write(chan, function, value);
		return;
	}

	if (chan) {
		ast_channel_lock(chan);
		headp = ast_channel_varshead(chan);
	} else {
		ast_rwlock_wrlock(globalslock);
		headp = globals;
	}

	if (value && (newvariable = ast_var_assign(name, value))) {
		if (headp == globals)
			ast_verb(2, "Setting global variable '%s' to '%s'\n", name, value);
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}

	if (chan)
		ast_channel_unlock(chan);
	else
		ast_rwlock_unlock(globalslock);
}

int pbx_builtin_setvar_helper(struct ast_channel *chan, const char *name, const char *value)
{
	struct ast_var_t *newvariable;
	struct varshead *headp;
	const char *nametail = name;
	/*! True if the old value was not an empty string. */
	int old_value_existed = 0;

	if (name[strlen(name) - 1] == ')') {
		char *function = ast_strdupa(name);

		return ast_func_write(chan, function, value);
	}

	if (chan) {
		ast_channel_lock(chan);
		headp = ast_channel_varshead(chan);
	} else {
		ast_rwlock_wrlock(globalslock);
		headp = globals;
	}

	/* For comparison purposes, we have to strip leading underscores */
	if (*nametail == '_') {
		nametail++;
		if (*nametail == '_')
			nametail++;
	}

	AST_LIST_TRAVERSE_SAFE_BEGIN(headp, newvariable, entries) {
		if (strcmp(ast_var_name(newvariable), nametail) == 0) {
			/* there is already such a variable, delete it */
			AST_LIST_REMOVE_CURRENT(entries);
			old_value_existed = !ast_strlen_zero(ast_var_value(newvariable));
			ast_var_delete(newvariable);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (value && (newvariable = ast_var_assign(name, value))) {
		if (headp == globals) {
			ast_verb(2, "Setting global variable '%s' to '%s'\n", name, value);
		}
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
		ast_channel_publish_varset(chan, name, value);
	} else if (old_value_existed) {
		/* We just deleted a non-empty dialplan variable. */
		ast_channel_publish_varset(chan, name, "");
	}

	if (chan)
		ast_channel_unlock(chan);
	else
		ast_rwlock_unlock(globalslock);
	return 0;
}

int pbx_builtin_setvar(struct ast_channel *chan, const char *data)
{
	char *name, *value, *mydata;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Set requires one variable name/value pair.\n");
		return 0;
	}

	mydata = ast_strdupa(data);
	name = strsep(&mydata, "=");
	value = mydata;
	if (!value) {
		ast_log(LOG_WARNING, "Set requires an '=' to be a valid assignment.\n");
		return 0;
	}

	if (strchr(name, ' ')) {
		ast_log(LOG_WARNING, "Please avoid unnecessary spaces on variables as it may lead to unexpected results ('%s' set to '%s').\n", name, mydata);
	}

	pbx_builtin_setvar_helper(chan, name, value);

	return 0;
}

int pbx_builtin_setvar_multiple(struct ast_channel *chan, const char *vdata)
{
	char *data;
	int x;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(pair)[24];
	);
	AST_DECLARE_APP_ARGS(pair,
		AST_APP_ARG(name);
		AST_APP_ARG(value);
	);

	if (ast_strlen_zero(vdata)) {
		ast_log(LOG_WARNING, "MSet requires at least one variable name/value pair.\n");
		return 0;
	}

	data = ast_strdupa(vdata);
	AST_STANDARD_APP_ARGS(args, data);

	for (x = 0; x < args.argc; x++) {
		AST_NONSTANDARD_APP_ARGS(pair, args.pair[x], '=');
		if (pair.argc == 2) {
			pbx_builtin_setvar_helper(chan, pair.name, pair.value);
			if (strchr(pair.name, ' '))
				ast_log(LOG_WARNING, "Please avoid unnecessary spaces on variables as it may lead to unexpected results ('%s' set to '%s').\n", pair.name, pair.value);
		} else if (!chan) {
			ast_log(LOG_WARNING, "MSet: ignoring entry '%s' with no '='\n", pair.name);
		} else {
			ast_log(LOG_WARNING, "MSet: ignoring entry '%s' with no '=' (in %s@%s:%d\n", pair.name, ast_channel_exten(chan), ast_channel_context(chan), ast_channel_priority(chan));
		}
	}

	return 0;
}

static int pbx_builtin_importvar(struct ast_channel *chan, const char *data)
{
	char *name;
	char *value;
	char *channel;
	char tmp[VAR_BUF_SIZE];
	static int deprecation_warning = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Ignoring, since there is no variable to set\n");
		return 0;
	}
	tmp[0] = 0;
	if (!deprecation_warning) {
		ast_log(LOG_WARNING, "ImportVar is deprecated.  Please use Set(varname=${IMPORT(channel,variable)}) instead.\n");
		deprecation_warning = 1;
	}

	value = ast_strdupa(data);
	name = strsep(&value,"=");
	channel = strsep(&value,",");
	if (channel && value && name) { /*! \todo XXX should do !ast_strlen_zero(..) of the args ? */
		struct ast_channel *chan2 = ast_channel_get_by_name(channel);
		if (chan2) {
			char *s = ast_alloca(strlen(value) + 4);
			sprintf(s, "${%s}", value);
			pbx_substitute_variables_helper(chan2, s, tmp, sizeof(tmp) - 1);
			chan2 = ast_channel_unref(chan2);
		}
		pbx_builtin_setvar_helper(chan, name, tmp);
	}

	return(0);
}

static int pbx_builtin_noop(struct ast_channel *chan, const char *data)
{
	return 0;
}

void pbx_builtin_clear_globals(void)
{
	struct ast_var_t *vardata;

	ast_rwlock_wrlock(globalslock);
	while ((vardata = AST_LIST_REMOVE_HEAD(globals, entries)))
		ast_var_delete(vardata);
	ast_rwlock_unlock(globalslock);
}

int pbx_checkcondition(const char *condition)
{
	int res;
	if (ast_strlen_zero(condition)) {                /* NULL or empty strings are false */
		return 0;
	} else if (sscanf(condition, "%30d", &res) == 1) { /* Numbers are evaluated for truth */
		return res;
	} else {                                         /* Strings are true */
		return 1;
	}
}

static int pbx_builtin_gotoif(struct ast_channel *chan, const char *data)
{
	char *condition, *branch1, *branch2, *branch;
	char *stringp;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Ignoring, since there is no variable to check\n");
		return 0;
	}

	stringp = ast_strdupa(data);
	condition = strsep(&stringp,"?");
	branch1 = strsep(&stringp,":");
	branch2 = strsep(&stringp,"");
	branch = pbx_checkcondition(condition) ? branch1 : branch2;

	if (ast_strlen_zero(branch)) {
		ast_debug(1, "Not taking any branch\n");
		return 0;
	}

	return pbx_builtin_goto(chan, branch);
}

static int pbx_builtin_saynumber(struct ast_channel *chan, const char *data)
{
	char tmp[256];
	char *number = tmp;
	int number_val;
	char *options;
	int res;
	int interrupt = 0;
	const char *interrupt_string;

	ast_channel_lock(chan);
	interrupt_string = pbx_builtin_getvar_helper(chan, "SAY_DTMF_INTERRUPT");
	if (ast_true(interrupt_string)) {
		interrupt = 1;
	}
	ast_channel_unlock(chan);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SayNumber requires an argument (number)\n");
		return -1;
	}
	ast_copy_string(tmp, data, sizeof(tmp));
	strsep(&number, ",");

	if (sscanf(tmp, "%d", &number_val) != 1) {
		ast_log(LOG_WARNING, "argument '%s' to SayNumber could not be parsed as a number.\n", tmp);
		return 0;
	}

	options = strsep(&number, ",");
	if (options) {
		if ( strcasecmp(options, "f") && strcasecmp(options, "m") &&
			strcasecmp(options, "c") && strcasecmp(options, "n") ) {
			ast_log(LOG_WARNING, "SayNumber gender option is either 'f', 'm', 'c' or 'n'\n");
			return -1;
		}
	}

	res = ast_say_number(chan, number_val, interrupt ? AST_DIGIT_ANY : "", ast_channel_language(chan), options);

	if (res < 0) {
		ast_log(LOG_WARNING, "We were unable to say the number %s, is it too large?\n", tmp);
	}

	return interrupt ? res : 0;
}

static int pbx_builtin_saydigits(struct ast_channel *chan, const char *data)
{
	int res = 0;
	int interrupt = 0;
	const char *interrupt_string;

	ast_channel_lock(chan);
	interrupt_string = pbx_builtin_getvar_helper(chan, "SAY_DTMF_INTERRUPT");
	if (ast_true(interrupt_string)) {
		interrupt = 1;
	}
	ast_channel_unlock(chan);

	if (data) {
		res = ast_say_digit_str(chan, data, interrupt ? AST_DIGIT_ANY : "", ast_channel_language(chan));
	}

	return res;
}

static int pbx_builtin_saycharacters_case(struct ast_channel *chan, const char *data)
{
	int res = 0;
	int sensitivity = 0;
	char *parse;
	int interrupt = 0;
	const char *interrupt_string;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(options);
		AST_APP_ARG(characters);
	);

	ast_channel_lock(chan);
	interrupt_string = pbx_builtin_getvar_helper(chan, "SAY_DTMF_INTERRUPT");
	if (ast_true(interrupt_string)) {
		interrupt = 1;
	}
	ast_channel_unlock(chan);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SayAlphaCase requires two arguments (options, characters)\n");
		return 0;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (!args.options || strlen(args.options) != 1) {
		ast_log(LOG_WARNING, "SayAlphaCase options are mutually exclusive and required\n");
		return 0;
	}

	switch (args.options[0]) {
	case 'a':
		sensitivity = AST_SAY_CASE_ALL;
		break;
	case 'l':
		sensitivity = AST_SAY_CASE_LOWER;
		break;
	case 'n':
		sensitivity = AST_SAY_CASE_NONE;
		break;
	case 'u':
		sensitivity = AST_SAY_CASE_UPPER;
		break;
	default:
		ast_log(LOG_WARNING, "Invalid option: '%s'\n", args.options);
		return 0;
	}

	res = ast_say_character_str(chan, args.characters, interrupt ? AST_DIGIT_ANY : "", ast_channel_language(chan), sensitivity);

	return res;
}

static int pbx_builtin_saycharacters(struct ast_channel *chan, const char *data)
{
	int res = 0;
	int interrupt = 0;
	const char *interrupt_string;

	ast_channel_lock(chan);
	interrupt_string = pbx_builtin_getvar_helper(chan, "SAY_DTMF_INTERRUPT");
	if (ast_true(interrupt_string)) {
		interrupt = 1;
	}
	ast_channel_unlock(chan);

	if (data) {
		res = ast_say_character_str(chan, data, interrupt ? AST_DIGIT_ANY : "", ast_channel_language(chan), AST_SAY_CASE_NONE);
	}

	return res;
}

static int pbx_builtin_sayphonetic(struct ast_channel *chan, const char *data)
{
	int res = 0;
	int interrupt = 0;
	const char *interrupt_string;

	ast_channel_lock(chan);
	interrupt_string = pbx_builtin_getvar_helper(chan, "SAY_DTMF_INTERRUPT");
	if (ast_true(interrupt_string)) {
		interrupt = 1;
	}
	ast_channel_unlock(chan);

	if (data)
		res = ast_say_phonetic_str(chan, data, interrupt ? AST_DIGIT_ANY : "", ast_channel_language(chan));
	return res;
}

void unload_pbx_builtins(void)
{
	int x;
	/* Unregister builtin applications */
	for (x = 0; x < ARRAY_LEN(builtins); x++) {
		ast_unregister_application(builtins[x].name);
	}
}

int load_pbx_builtins(struct varshead *g, ast_rwlock_t *l)
{
	int x;

	globals = g;
	globalslock = l;

	/* Register builtin applications */
	for (x = 0; x < ARRAY_LEN(builtins); x++) {
		if (ast_register_application2(builtins[x].name, builtins[x].execute, NULL, NULL, NULL)) {
			ast_log(LOG_ERROR, "Unable to register builtin application '%s'\n", builtins[x].name);
			return -1;
		}
	}
	return 0;
}
