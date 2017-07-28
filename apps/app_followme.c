/*
 * Asterisk -- An open source telephony toolkit.
 *
 * A full-featured Find-Me/Follow-Me Application
 * 
 * Copyright (C) 2005-2006, BJ Weschke All Rights Reserved.
 *
 * BJ Weschke <bweschke@btwtech.com>
 *
 * This code is released by the author with no restrictions on usage.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 *
 * \brief Find-Me Follow-Me application
 *
 * \author BJ Weschke <bweschke@btwtech.com>
 *
 * \ingroup applications
 */

/*! \li \ref app_followme.c uses the configuration file \ref followme.conf
 * \addtogroup configuration_file Configuration Files
 */

/*! 
 * \page followme.conf followme.conf
 * \verbinclude followme.conf.sample
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <signal.h>

#include "asterisk/paths.h"	/* use ast_config_AST_SPOOL_DIR */
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/say.h"
#include "asterisk/features.h"
#include "asterisk/musiconhold.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/astdb.h"
#include "asterisk/dsp.h"
#include "asterisk/app.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/max_forwards.h"

#define REC_FORMAT "sln"

/*** DOCUMENTATION
	<application name="FollowMe" language="en_US">
		<synopsis>
			Find-Me/Follow-Me application.
		</synopsis>
		<syntax>
			<parameter name="followmeid" required="true" />
			<parameter name="options">
				<optionlist>
					<option name="a">
						<para>Record the caller's name so it can be announced to the
						callee on each step.</para>
					</option>
					<option name="B" argsep="^">
						<para>Before initiating the outgoing call(s), Gosub to the specified
						location using the current channel.</para>
						<argument name="context" required="false" />
						<argument name="exten" required="false" />
						<argument name="priority" required="true" hasparams="optional" argsep="^">
							<argument name="arg1" multiple="true" required="true" />
							<argument name="argN" />
						</argument>
					</option>
					<option name="b" argsep="^">
						<para>Before initiating an outgoing call, Gosub to the specified
						location using the newly created channel.  The Gosub will be
						executed for each destination channel.</para>
						<argument name="context" required="false" />
						<argument name="exten" required="false" />
						<argument name="priority" required="true" hasparams="optional" argsep="^">
							<argument name="arg1" multiple="true" required="true" />
							<argument name="argN" />
						</argument>
					</option>
					<option name="d">
						<para>Disable the 'Please hold while we try to connect your call' announcement.</para>
					</option>
					<option name="I">
						<para>Asterisk will ignore any connected line update requests
						it may receive on this dial attempt.</para>
					</option>
					<option name="l">
						<para>Disable local call optimization so that applications with
						audio hooks between the local bridge don't get dropped when the
						calls get joined directly.</para>
					</option>
					<option name="N">
						<para>Don't answer the incoming call until we're ready to
						connect the caller or give up.</para>
						<note>
	 						<para>This option is ignored if the call is already answered.</para>
 						</note>
						<note>
							<para>If the call is not already answered, the 'a' and 's'
							options are ignored while the 'd' option is implicitly enabled.</para>
 						</note>
					</option>
					<option name="n">
						<para>Playback the unreachable status message if we've run out
						of steps or the callee has elected not to be reachable.</para>
					</option>
					<option name="s">
						<para>Playback the incoming status message prior to starting
						the follow-me step(s)</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application performs Find-Me/Follow-Me functionality for the caller
			as defined in the profile matching the <replaceable>followmeid</replaceable> parameter in
			<filename>followme.conf</filename>. If the specified <replaceable>followmeid</replaceable>
			profile doesn't exist in <filename>followme.conf</filename>, execution will be returned
			to the dialplan and call execution will continue at the next priority.</para>
			<para>Returns -1 on hangup.</para>
		</description>
	</application>
 ***/

static char *app = "FollowMe";

/*! Maximum accept/decline DTMF string plus terminator. */
#define MAX_YN_STRING		20

/*! \brief Number structure */
struct number {
	char number[512];	/*!< Phone Number(s) and/or Extension(s) */
	long timeout;		/*!< Dial Timeout, if used. */
	int order;		/*!< The order to dial in */
	AST_LIST_ENTRY(number) entry; /*!< Next Number record */
};

/*! \brief Data structure for followme scripts */
struct call_followme {
	ast_mutex_t lock;
	char name[AST_MAX_EXTENSION];	/*!< Name - FollowMeID */
	char moh[MAX_MUSICCLASS];	/*!< Music On Hold Class to be used */
	char context[AST_MAX_CONTEXT];  /*!< Context to dial from */
	unsigned int active;		/*!< Profile is active (1), or disabled (0). */
	int realtime;           /*!< Cached from realtime */
	/*! Allow callees to accept/reject the forwarded call */
	unsigned int enable_callee_prompt:1;
	char takecall[MAX_YN_STRING];	/*!< Digit mapping to take a call */
	char nextindp[MAX_YN_STRING];	/*!< Digit mapping to decline a call */
	char callfromprompt[PATH_MAX];	/*!< Sound prompt name and path */
	char norecordingprompt[PATH_MAX];	/*!< Sound prompt name and path */
	char optionsprompt[PATH_MAX];	/*!< Sound prompt name and path */
	char plsholdprompt[PATH_MAX];	/*!< Sound prompt name and path */
	char statusprompt[PATH_MAX];	/*!< Sound prompt name and path */
	char sorryprompt[PATH_MAX];	/*!< Sound prompt name and path */

	AST_LIST_HEAD_NOLOCK(numbers, number) numbers;	   /*!< Head of the list of follow-me numbers */
	AST_LIST_HEAD_NOLOCK(blnumbers, number) blnumbers; /*!< Head of the list of black-listed numbers */
	AST_LIST_HEAD_NOLOCK(wlnumbers, number) wlnumbers; /*!< Head of the list of white-listed numbers */
	AST_LIST_ENTRY(call_followme) entry;           /*!< Next Follow-Me record */
};

struct fm_args {
	char *mohclass;
	AST_LIST_HEAD_NOLOCK(cnumbers, number) cnumbers;
	/*! Gosub app arguments for outgoing calls.  NULL if not supplied. */
	const char *predial_callee;
	/*! Accumulated connected line information from inbound call. */
	struct ast_party_connected_line connected_in;
	/*! Accumulated connected line information from outbound call. */
	struct ast_party_connected_line connected_out;
	/*! TRUE if connected line information from inbound call changed. */
	unsigned int pending_in_connected_update:1;
	/*! TRUE if connected line information from outbound call is available. */
	unsigned int pending_out_connected_update:1;
	/*! TRUE if caller has a pending hold request for the winning call. */
	unsigned int pending_hold:1;
	/*! TRUE if callees will be prompted to answer */
	unsigned int enable_callee_prompt:1;
	/*! Music On Hold Class suggested by caller hold for winning call. */
	char suggested_moh[MAX_MUSICCLASS];
	char context[AST_MAX_CONTEXT];
	char namerecloc[PATH_MAX];
	char takecall[MAX_YN_STRING];	/*!< Digit mapping to take a call */
	char nextindp[MAX_YN_STRING];	/*!< Digit mapping to decline a call */
	char callfromprompt[PATH_MAX];	/*!< Sound prompt name and path */
	char norecordingprompt[PATH_MAX];	/*!< Sound prompt name and path */
	char optionsprompt[PATH_MAX];	/*!< Sound prompt name and path */
	char plsholdprompt[PATH_MAX];	/*!< Sound prompt name and path */
	char statusprompt[PATH_MAX];	/*!< Sound prompt name and path */
	char sorryprompt[PATH_MAX];	/*!< Sound prompt name and path */
	struct ast_flags followmeflags;
};

struct findme_user {
	struct ast_channel *ochan;
	/*! Accumulated connected line information from outgoing call. */
	struct ast_party_connected_line connected;
	long digts;
	int ynidx;
	int state;
	char dialarg[256];
	/*! Collected digits to accept/decline the call. */
	char yn[MAX_YN_STRING];
	/*! TRUE if the outgoing call is answered. */
	unsigned int answered:1;
	/*! TRUE if connected line information is available. */
	unsigned int pending_connected_update:1;
	AST_LIST_ENTRY(findme_user) entry;
};

enum {
	FOLLOWMEFLAG_STATUSMSG = (1 << 0),
	FOLLOWMEFLAG_RECORDNAME = (1 << 1),
	FOLLOWMEFLAG_UNREACHABLEMSG = (1 << 2),
	FOLLOWMEFLAG_DISABLEHOLDPROMPT = (1 << 3),
	FOLLOWMEFLAG_NOANSWER = (1 << 4),
	FOLLOWMEFLAG_DISABLEOPTIMIZATION = (1 << 5),
	FOLLOWMEFLAG_IGNORE_CONNECTEDLINE = (1 << 6),
	FOLLOWMEFLAG_PREDIAL_CALLER = (1 << 7),
	FOLLOWMEFLAG_PREDIAL_CALLEE = (1 << 8),
};

enum {
	FOLLOWMEFLAG_ARG_PREDIAL_CALLER,
	FOLLOWMEFLAG_ARG_PREDIAL_CALLEE,

	/* note: this entry _MUST_ be the last one in the enum */
	FOLLOWMEFLAG_ARG_ARRAY_SIZE
};

AST_APP_OPTIONS(followme_opts, {
	AST_APP_OPTION('a', FOLLOWMEFLAG_RECORDNAME),
	AST_APP_OPTION_ARG('B', FOLLOWMEFLAG_PREDIAL_CALLER, FOLLOWMEFLAG_ARG_PREDIAL_CALLER),
	AST_APP_OPTION_ARG('b', FOLLOWMEFLAG_PREDIAL_CALLEE, FOLLOWMEFLAG_ARG_PREDIAL_CALLEE),
	AST_APP_OPTION('d', FOLLOWMEFLAG_DISABLEHOLDPROMPT),
	AST_APP_OPTION('I', FOLLOWMEFLAG_IGNORE_CONNECTEDLINE),
	AST_APP_OPTION('l', FOLLOWMEFLAG_DISABLEOPTIMIZATION),
	AST_APP_OPTION('N', FOLLOWMEFLAG_NOANSWER),
	AST_APP_OPTION('n', FOLLOWMEFLAG_UNREACHABLEMSG),
	AST_APP_OPTION('s', FOLLOWMEFLAG_STATUSMSG),
});

static const char *featuredigittostr;
static int featuredigittimeout = 5000;		/*!< Feature Digit Timeout */
static const char *defaultmoh = "default";    	/*!< Default Music-On-Hold Class */

static char takecall[MAX_YN_STRING] = "1";
static char nextindp[MAX_YN_STRING] = "2";
static int enable_callee_prompt = 1;
static char callfromprompt[PATH_MAX] = "followme/call-from";
static char norecordingprompt[PATH_MAX] = "followme/no-recording";
static char optionsprompt[PATH_MAX] = "followme/options";
static char plsholdprompt[PATH_MAX] = "followme/pls-hold-while-try";
static char statusprompt[PATH_MAX] = "followme/status";
static char sorryprompt[PATH_MAX] = "followme/sorry";


static AST_RWLIST_HEAD_STATIC(followmes, call_followme);
AST_LIST_HEAD_NOLOCK(findme_user_listptr, findme_user);

static void free_numbers(struct call_followme *f)
{
	/* Free numbers attached to the profile */
	struct number *prev;

	while ((prev = AST_LIST_REMOVE_HEAD(&f->numbers, entry)))
		/* Free the number */
		ast_free(prev);
	AST_LIST_HEAD_INIT_NOLOCK(&f->numbers);

	while ((prev = AST_LIST_REMOVE_HEAD(&f->blnumbers, entry)))
		/* Free the blacklisted number */
		ast_free(prev);
	AST_LIST_HEAD_INIT_NOLOCK(&f->blnumbers);

	while ((prev = AST_LIST_REMOVE_HEAD(&f->wlnumbers, entry)))
		/* Free the whitelisted number */
		ast_free(prev);
	AST_LIST_HEAD_INIT_NOLOCK(&f->wlnumbers);
}


/*! \brief Allocate and initialize followme profile */
static struct call_followme *alloc_profile(const char *fmname)
{
	struct call_followme *f;

	if (!(f = ast_calloc(1, sizeof(*f))))
		return NULL;

	ast_mutex_init(&f->lock);
	ast_copy_string(f->name, fmname, sizeof(f->name));
	AST_LIST_HEAD_INIT_NOLOCK(&f->numbers);
	AST_LIST_HEAD_INIT_NOLOCK(&f->blnumbers);
	AST_LIST_HEAD_INIT_NOLOCK(&f->wlnumbers);
	return f;
}

static void init_profile(struct call_followme *f, int activate)
{
	f->enable_callee_prompt = enable_callee_prompt;
	f->context[0] = '\0';
	ast_copy_string(f->moh, defaultmoh, sizeof(f->moh));
	ast_copy_string(f->takecall, takecall, sizeof(f->takecall));
	ast_copy_string(f->nextindp, nextindp, sizeof(f->nextindp));
	ast_copy_string(f->callfromprompt, callfromprompt, sizeof(f->callfromprompt));
	ast_copy_string(f->norecordingprompt, norecordingprompt, sizeof(f->norecordingprompt));
	ast_copy_string(f->optionsprompt, optionsprompt, sizeof(f->optionsprompt));
	ast_copy_string(f->plsholdprompt, plsholdprompt, sizeof(f->plsholdprompt));
	ast_copy_string(f->statusprompt, statusprompt, sizeof(f->statusprompt));
	ast_copy_string(f->sorryprompt, sorryprompt, sizeof(f->sorryprompt));
	if (activate) {
		f->active = 1;
	}
}

   
   
/*! \brief Set parameter in profile from configuration file */
static void profile_set_param(struct call_followme *f, const char *param, const char *val, int linenum, int failunknown)
{

	if (!strcasecmp(param, "musicclass") || !strcasecmp(param, "musiconhold") || !strcasecmp(param, "music")) 
		ast_copy_string(f->moh, val, sizeof(f->moh));
	else if (!strcasecmp(param, "context")) 
		ast_copy_string(f->context, val, sizeof(f->context));
	else if (!strcasecmp(param, "enable_callee_prompt"))
		f->enable_callee_prompt = ast_true(val);
	else if (!strcasecmp(param, "takecall"))
		ast_copy_string(f->takecall, val, sizeof(f->takecall));
	else if (!strcasecmp(param, "declinecall"))
		ast_copy_string(f->nextindp, val, sizeof(f->nextindp));
	else if (!strcasecmp(param, "call-from-prompt") || !strcasecmp(param, "call_from_prompt"))
		ast_copy_string(f->callfromprompt, val, sizeof(f->callfromprompt));
	else if (!strcasecmp(param, "followme-norecording-prompt") || !strcasecmp(param, "norecording_prompt")) 
		ast_copy_string(f->norecordingprompt, val, sizeof(f->norecordingprompt));
	else if (!strcasecmp(param, "followme-options-prompt") || !strcasecmp(param, "options_prompt")) 
		ast_copy_string(f->optionsprompt, val, sizeof(f->optionsprompt));
	else if (!strcasecmp(param, "followme-pls-hold-prompt") || !strcasecmp(param, "pls_hold_prompt"))
		ast_copy_string(f->plsholdprompt, val, sizeof(f->plsholdprompt));
	else if (!strcasecmp(param, "followme-status-prompt") || !strcasecmp(param, "status_prompt")) 
		ast_copy_string(f->statusprompt, val, sizeof(f->statusprompt));
	else if (!strcasecmp(param, "followme-sorry-prompt") || !strcasecmp(param, "sorry_prompt")) 
		ast_copy_string(f->sorryprompt, val, sizeof(f->sorryprompt));
	else if (failunknown) {
		if (linenum >= 0)
			ast_log(LOG_WARNING, "Unknown keyword in profile '%s': %s at line %d of followme.conf\n", f->name, param, linenum);
		else
			ast_log(LOG_WARNING, "Unknown keyword in profile '%s': %s\n", f->name, param);
	}
}

/*! \brief Add a new number */
static struct number *create_followme_number(const char *number, int timeout, int numorder)
{
	struct number *cur;
	char *buf = ast_strdupa(number);
	char *tmp;

	if (!(cur = ast_calloc(1, sizeof(*cur))))
		return NULL;

	cur->timeout = timeout;
	if ((tmp = strchr(buf, ',')))
		*tmp = '\0';
	ast_copy_string(cur->number, buf, sizeof(cur->number));
	cur->order = numorder;
	ast_debug(1, "Created a number, %s, order of , %d, with a timeout of %ld.\n", cur->number, cur->order, cur->timeout);

	return cur;
}

/*! \brief Reload followme application module */
static int reload_followme(int reload)
{
	struct call_followme *f;
	struct ast_config *cfg;
	char *cat = NULL, *tmp;
	struct ast_variable *var;
	struct number *cur, *nm;
	char *numberstr;
	int timeout;
	int numorder;
	const char* enable_callee_prompt_str;
	const char *takecallstr;
	const char *declinecallstr;
	const char *tmpstr;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	if (!(cfg = ast_config_load("followme.conf", config_flags))) {
		ast_log(LOG_WARNING, "No follow me config file (followme.conf), so no follow me\n");
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file followme.conf is in an invalid format.  Aborting.\n");
		return 0;
	}

	AST_RWLIST_WRLOCK(&followmes);

	/* Reset Global Var Values */
	featuredigittimeout = 5000;

	/* Mark all profiles as inactive for the moment */
	AST_RWLIST_TRAVERSE(&followmes, f, entry) {
		f->active = 0;
	}

	featuredigittostr = ast_variable_retrieve(cfg, "general", "featuredigittimeout");

	if (!ast_strlen_zero(featuredigittostr)) {
		if (!sscanf(featuredigittostr, "%30d", &featuredigittimeout))
			featuredigittimeout = 5000;
	}

	if ((enable_callee_prompt_str = ast_variable_retrieve(cfg, "general",
					"enable_callee_prompt")) &&
			!ast_strlen_zero(enable_callee_prompt_str)) {
		enable_callee_prompt = ast_true(enable_callee_prompt_str);
	}

	if ((takecallstr = ast_variable_retrieve(cfg, "general", "takecall")) && !ast_strlen_zero(takecallstr)) {
		ast_copy_string(takecall, takecallstr, sizeof(takecall));
	}

	if ((declinecallstr = ast_variable_retrieve(cfg, "general", "declinecall")) && !ast_strlen_zero(declinecallstr)) {
		ast_copy_string(nextindp, declinecallstr, sizeof(nextindp));
	}

	if ((tmpstr = ast_variable_retrieve(cfg, "general", "call-from-prompt")) && !ast_strlen_zero(tmpstr)) {
		ast_copy_string(callfromprompt, tmpstr, sizeof(callfromprompt));
	} else if ((tmpstr = ast_variable_retrieve(cfg, "general", "call_from_prompt")) && !ast_strlen_zero(tmpstr)) {
		ast_copy_string(callfromprompt, tmpstr, sizeof(callfromprompt));
	}

	if ((tmpstr = ast_variable_retrieve(cfg, "general", "norecording-prompt")) && !ast_strlen_zero(tmpstr)) {
		ast_copy_string(norecordingprompt, tmpstr, sizeof(norecordingprompt));
	} else if ((tmpstr = ast_variable_retrieve(cfg, "general", "norecording_prompt")) && !ast_strlen_zero(tmpstr)) {
		ast_copy_string(norecordingprompt, tmpstr, sizeof(norecordingprompt));
	}


	if ((tmpstr = ast_variable_retrieve(cfg, "general", "options-prompt")) && !ast_strlen_zero(tmpstr)) {
		ast_copy_string(optionsprompt, tmpstr, sizeof(optionsprompt));
	} else if ((tmpstr = ast_variable_retrieve(cfg, "general", "options_prompt")) && !ast_strlen_zero(tmpstr)) {
		ast_copy_string(optionsprompt, tmpstr, sizeof(optionsprompt));
	}

	if ((tmpstr = ast_variable_retrieve(cfg, "general", "pls-hold-prompt")) && !ast_strlen_zero(tmpstr)) {
		ast_copy_string(plsholdprompt, tmpstr, sizeof(plsholdprompt));
	} else if ((tmpstr = ast_variable_retrieve(cfg, "general", "pls_hold_prompt")) && !ast_strlen_zero(tmpstr)) {
		ast_copy_string(plsholdprompt, tmpstr, sizeof(plsholdprompt));
	}

	if ((tmpstr = ast_variable_retrieve(cfg, "general", "status-prompt")) && !ast_strlen_zero(tmpstr)) {
		ast_copy_string(statusprompt, tmpstr, sizeof(statusprompt));
	} else if ((tmpstr = ast_variable_retrieve(cfg, "general", "status_prompt")) && !ast_strlen_zero(tmpstr)) {
		ast_copy_string(statusprompt, tmpstr, sizeof(statusprompt));
	}

	if ((tmpstr = ast_variable_retrieve(cfg, "general", "sorry-prompt")) && !ast_strlen_zero(tmpstr)) {
		ast_copy_string(sorryprompt, tmpstr, sizeof(sorryprompt));
	} else if ((tmpstr = ast_variable_retrieve(cfg, "general", "sorry_prompt")) && !ast_strlen_zero(tmpstr)) {
		ast_copy_string(sorryprompt, tmpstr, sizeof(sorryprompt));
	}

	/* Chug through config file */
	while ((cat = ast_category_browse(cfg, cat))) {
		int new = 0;

		if (!strcasecmp(cat, "general"))
			continue;

		/* Look for an existing one */
		AST_LIST_TRAVERSE(&followmes, f, entry) {
			if (!strcasecmp(f->name, cat))
				break;
		}

		ast_debug(1, "New profile %s.\n", cat);

		if (!f) {
			/* Make one then */
			f = alloc_profile(cat);
			new = 1;
		}

		/* Totally fail if we fail to find/create an entry */
		if (!f)
			continue;

		if (!new)
			ast_mutex_lock(&f->lock);
		/* Re-initialize the profile */
		init_profile(f, 1);
		free_numbers(f);
		var = ast_variable_browse(cfg, cat);
		while (var) {
			if (!strcasecmp(var->name, "number")) {
				int idx = 0;

				/* Add a new number */
				numberstr = ast_strdupa(var->value);
				if ((tmp = strchr(numberstr, ','))) {
					*tmp++ = '\0';
					timeout = atoi(tmp);
					if (timeout < 0) {
						timeout = 25;
					}
					if ((tmp = strchr(tmp, ','))) {
						*tmp++ = '\0';
						numorder = atoi(tmp);
						if (numorder < 0)
							numorder = 0;
					} else 
						numorder = 0;
				} else {
					timeout = 25;
					numorder = 0;
				}

				if (!numorder) {
					idx = 1;
					AST_LIST_TRAVERSE(&f->numbers, nm, entry) 
						idx++;
					numorder = idx;
				}
				cur = create_followme_number(numberstr, timeout, numorder);
				if (cur) {
					AST_LIST_INSERT_TAIL(&f->numbers, cur, entry);
				}
			} else {
				profile_set_param(f, var->name, var->value, var->lineno, 1);
				ast_debug(2, "Logging parameter %s with value %s from lineno %d\n", var->name, var->value, var->lineno);
			}
			var = var->next;
		} /* End while(var) loop */

		if (!new) 
			ast_mutex_unlock(&f->lock);
		else
			AST_RWLIST_INSERT_HEAD(&followmes, f, entry);
	}

	ast_config_destroy(cfg);

	AST_RWLIST_UNLOCK(&followmes);

	return 1;
}

static void publish_dial_end_event(struct ast_channel *in, struct findme_user_listptr *findme_user_list, struct ast_channel *exception, const char *status)
{
	struct findme_user *tmpuser;

	AST_LIST_TRAVERSE(findme_user_list, tmpuser, entry) {
		if (tmpuser->ochan && tmpuser->ochan != exception) {
			ast_channel_publish_dial(in, tmpuser->ochan, NULL, status);
		}
	}
}

static void clear_caller(struct findme_user *tmpuser)
{
	struct ast_channel *outbound;

	if (!tmpuser->ochan) {
		/* Call already cleared. */
		return;
	}

	outbound = tmpuser->ochan;
	ast_hangup(outbound);
	tmpuser->ochan = NULL;
}

static void clear_unanswered_calls(struct findme_user_listptr *findme_user_list) 
{
	struct findme_user *tmpuser;

	AST_LIST_TRAVERSE(findme_user_list, tmpuser, entry) {
		if (!tmpuser->answered) {
			clear_caller(tmpuser);
		}
	}
}

static void destroy_calling_node(struct findme_user *node)
{
	clear_caller(node);
	ast_party_connected_line_free(&node->connected);
	ast_free(node);
}

static void destroy_calling_tree(struct findme_user_listptr *findme_user_list)
{
	struct findme_user *fmuser;

	while ((fmuser = AST_LIST_REMOVE_HEAD(findme_user_list, entry))) {
		destroy_calling_node(fmuser);
	}
}

static struct ast_channel *wait_for_winner(struct findme_user_listptr *findme_user_list, struct number *nm, struct ast_channel *caller, struct fm_args *tpargs)
{
	struct ast_party_connected_line connected;
	struct ast_channel *watchers[256];
	int pos;
	struct ast_channel *winner;
	struct ast_frame *f;
	struct findme_user *tmpuser;
	int to = 0;
	int livechannels;
	int tmpto;
	long totalwait = 0, wtd = 0, towas = 0;
	char *callfromname;
	char *pressbuttonname;

	/* ------------ wait_for_winner_channel start --------------- */ 

	callfromname = ast_strdupa(tpargs->callfromprompt);
	pressbuttonname = ast_strdupa(tpargs->optionsprompt);

	totalwait = nm->timeout * 1000;

	for (;;) {
		to = 1000;
		pos = 1; 
		livechannels = 0;
		watchers[0] = caller;

		winner = NULL;
		AST_LIST_TRAVERSE(findme_user_list, tmpuser, entry) {
			if (!tmpuser->ochan) {
				continue;
			}
			if (tmpuser->state == 3) {
				tmpuser->digts += (towas - wtd);
			}
			if (tmpuser->digts && (tmpuser->digts > featuredigittimeout)) {
				ast_verb(3, "<%s> We've been waiting for digits longer than we should have.\n",
					ast_channel_name(tmpuser->ochan));
				if (tpargs->enable_callee_prompt) {
					if (!ast_strlen_zero(tpargs->namerecloc)) {
						tmpuser->state = 1;
						tmpuser->digts = 0;
						if (!ast_streamfile(tmpuser->ochan, callfromname, ast_channel_language(tmpuser->ochan))) {
							ast_sched_runq(ast_channel_sched(tmpuser->ochan));
						} else {
							ast_log(LOG_WARNING, "Unable to playback %s.\n", callfromname);
							clear_caller(tmpuser);
							continue;
						}
					} else {
						tmpuser->state = 2;
						tmpuser->digts = 0;
						if (!ast_streamfile(tmpuser->ochan, tpargs->norecordingprompt, ast_channel_language(tmpuser->ochan)))
							ast_sched_runq(ast_channel_sched(tmpuser->ochan));
						else {
							ast_log(LOG_WARNING, "Unable to playback %s.\n", tpargs->norecordingprompt);
							clear_caller(tmpuser);
							continue;
						}
					}
				} else {
					tmpuser->state = 3;
				}
			}
			if (ast_channel_stream(tmpuser->ochan)) {
				ast_sched_runq(ast_channel_sched(tmpuser->ochan));
				tmpto = ast_sched_wait(ast_channel_sched(tmpuser->ochan));
				if (tmpto > 0 && tmpto < to)
					to = tmpto;
				else if (tmpto < 0 && !ast_channel_timingfunc(tmpuser->ochan)) {
					ast_stopstream(tmpuser->ochan);
					switch (tmpuser->state) {
					case 1:
						ast_verb(3, "<%s> Playback of the call-from file appears to be done.\n",
							ast_channel_name(tmpuser->ochan));
						if (!ast_streamfile(tmpuser->ochan, tpargs->namerecloc, ast_channel_language(tmpuser->ochan))) {
							tmpuser->state = 2;
						} else {
							ast_log(LOG_NOTICE, "<%s> Unable to playback %s. Maybe the caller didn't record their name?\n",
								ast_channel_name(tmpuser->ochan), tpargs->namerecloc);
							memset(tmpuser->yn, 0, sizeof(tmpuser->yn));
							tmpuser->ynidx = 0;
							if (!ast_streamfile(tmpuser->ochan, pressbuttonname, ast_channel_language(tmpuser->ochan)))
								tmpuser->state = 3;
							else {
								ast_log(LOG_WARNING, "Unable to playback %s.\n", pressbuttonname);
								clear_caller(tmpuser);
								continue;
							}
						}
						break;
					case 2:
						ast_verb(3, "<%s> Playback of name file appears to be done.\n",
							ast_channel_name(tmpuser->ochan));
						memset(tmpuser->yn, 0, sizeof(tmpuser->yn));
						tmpuser->ynidx = 0;
						if (!ast_streamfile(tmpuser->ochan, pressbuttonname, ast_channel_language(tmpuser->ochan))) {
							tmpuser->state = 3;
						} else {
							clear_caller(tmpuser);
							continue;
						}
						break;
					case 3:
						ast_verb(3, "<%s> Playback of the next step file appears to be done.\n",
							ast_channel_name(tmpuser->ochan));
						tmpuser->digts = 0;
						break;
					default:
						break;
					}
				}
			}
			watchers[pos++] = tmpuser->ochan;
			livechannels++;
		}
		if (!livechannels) {
			ast_verb(3, "No live channels left for this step.\n");
			return NULL;
		}

		tmpto = to;
		if (to < 0) {
			to = 1000;
			tmpto = 1000;
		}
		towas = to;
		winner = ast_waitfor_n(watchers, pos, &to);
		tmpto -= to;
		totalwait -= tmpto;
		wtd = to;
		if (totalwait <= 0) {
			ast_verb(3, "We've hit our timeout for this step. Dropping unanswered calls and starting the next step.\n");
			clear_unanswered_calls(findme_user_list);
			return NULL;
		}
		if (winner) {
			/* Need to find out which channel this is */
			if (winner != caller) {
				/* The winner is an outgoing channel. */
				AST_LIST_TRAVERSE(findme_user_list, tmpuser, entry) {
					if (tmpuser->ochan == winner) {
						break;
					}
				}
			} else {
				tmpuser = NULL;
			}

			f = ast_read(winner);
			if (f) {
				if (f->frametype == AST_FRAME_CONTROL) {
					switch (f->subclass.integer) {
					case AST_CONTROL_HANGUP:
						ast_verb(3, "%s received a hangup frame.\n", ast_channel_name(winner));
						if (f->data.uint32) {
							ast_channel_hangupcause_set(winner, f->data.uint32);
						}
						if (!tmpuser) {
							ast_verb(3, "The calling channel hungup. Need to drop everyone.\n");
							publish_dial_end_event(caller, findme_user_list, NULL, "CANCEL");
							ast_frfree(f);
							return NULL;
						}
						clear_caller(tmpuser);
						break;
					case AST_CONTROL_ANSWER:
						if (!tmpuser) {
							/* The caller answered?  We want an outgoing channel to answer. */
							break;
						}
						ast_verb(3, "%s answered %s\n", ast_channel_name(winner), ast_channel_name(caller));
						ast_channel_publish_dial(caller, winner, NULL, "ANSWER");
						publish_dial_end_event(caller, findme_user_list, winner, "CANCEL");
						tmpuser->answered = 1;
						/* If call has been answered, then the eventual hangup is likely to be normal hangup */ 
						ast_channel_hangupcause_set(winner, AST_CAUSE_NORMAL_CLEARING);
						ast_channel_hangupcause_set(caller, AST_CAUSE_NORMAL_CLEARING);
						if (tpargs->enable_callee_prompt) {
							ast_verb(3, "Starting playback of %s\n", callfromname);
							if (!ast_strlen_zero(tpargs->namerecloc)) {
								if (!ast_streamfile(winner, callfromname, ast_channel_language(winner))) {
									ast_sched_runq(ast_channel_sched(winner));
									tmpuser->state = 1;
								} else {
									ast_log(LOG_WARNING, "Unable to playback %s.\n", callfromname);
									clear_caller(tmpuser);
								}
							} else {
								tmpuser->state = 2;
								if (!ast_streamfile(tmpuser->ochan, tpargs->norecordingprompt, ast_channel_language(tmpuser->ochan)))
									ast_sched_runq(ast_channel_sched(tmpuser->ochan));
								else {
									ast_log(LOG_WARNING, "Unable to playback %s.\n", tpargs->norecordingprompt);
									clear_caller(tmpuser);
								}
							}
						} else {
							ast_verb(3, "Skip playback of caller name / norecording\n");
							tmpuser->state = 2;
						}
						break;
					case AST_CONTROL_BUSY:
						ast_verb(3, "%s is busy\n", ast_channel_name(winner));
						if (tmpuser) {
							/* Outbound call was busy.  Drop it. */
							ast_channel_publish_dial(caller, winner, NULL, "BUSY");
							clear_caller(tmpuser);
						}
						break;
					case AST_CONTROL_CONGESTION:
						ast_verb(3, "%s is circuit-busy\n", ast_channel_name(winner));
						if (tmpuser) {
							/* Outbound call was congested.  Drop it. */
							ast_channel_publish_dial(caller, winner, NULL, "CONGESTION");
							clear_caller(tmpuser);
						}
						break;
					case AST_CONTROL_RINGING:
						ast_verb(3, "%s is ringing\n", ast_channel_name(winner));
						ast_channel_publish_dial(caller, winner, NULL, "RINGING");
						break;
					case AST_CONTROL_PROGRESS:
						ast_verb(3, "%s is making progress\n", ast_channel_name(winner));
						ast_channel_publish_dial(caller, winner, NULL, "PROGRESS");
						break;
					case AST_CONTROL_VIDUPDATE:
						ast_verb(3, "%s requested a video update\n", ast_channel_name(winner));
						break;
					case AST_CONTROL_SRCUPDATE:
						ast_verb(3, "%s requested a source update\n", ast_channel_name(winner));
						break;
					case AST_CONTROL_PROCEEDING:
						ast_verb(3, "%s is proceeding\n", ast_channel_name(winner));
						ast_channel_publish_dial(caller, winner, NULL, "PROCEEDING");
						break;
					case AST_CONTROL_HOLD:
						ast_verb(3, "%s placed call on hold\n", ast_channel_name(winner));
						if (!tmpuser) {
							/* Caller placed outgoing calls on hold. */
							tpargs->pending_hold = 1;
							if (f->data.ptr) {
								ast_copy_string(tpargs->suggested_moh, f->data.ptr,
									sizeof(tpargs->suggested_moh));
							} else {
								tpargs->suggested_moh[0] = '\0';
							}
						} else {
							/*
							 * Outgoing call placed caller on hold.
							 *
							 * Ignore because the outgoing call should not be able to place
							 * the caller on hold until after they are bridged.
							 */
						}
						break;
					case AST_CONTROL_UNHOLD:
						ast_verb(3, "%s removed call from hold\n", ast_channel_name(winner));
						if (!tmpuser) {
							/* Caller removed outgoing calls from hold. */
							tpargs->pending_hold = 0;
						} else {
							/*
							 * Outgoing call removed caller from hold.
							 *
							 * Ignore because the outgoing call should not be able to place
							 * the caller on hold until after they are bridged.
							 */
						}
						break;
					case AST_CONTROL_OFFHOOK:
					case AST_CONTROL_FLASH:
						/* Ignore going off hook and flash */
						break;
					case AST_CONTROL_CONNECTED_LINE:
						if (!tmpuser) {
							/*
							 * Hold connected line update from caller until we have a
							 * winner.
							 */
							ast_verb(3,
								"%s connected line has changed. Saving it until we have a winner.\n",
								ast_channel_name(winner));
							ast_party_connected_line_set_init(&connected, &tpargs->connected_in);
							if (!ast_connected_line_parse_data(f->data.ptr, f->datalen, &connected)) {
								ast_party_connected_line_set(&tpargs->connected_in,
									&connected, NULL);
								tpargs->pending_in_connected_update = 1;
							}
							ast_party_connected_line_free(&connected);
							break;
						}
						if (ast_test_flag(&tpargs->followmeflags, FOLLOWMEFLAG_IGNORE_CONNECTEDLINE)) {
							ast_verb(3, "Connected line update from %s prevented.\n",
								ast_channel_name(winner));
						} else {
							ast_verb(3,
								"%s connected line has changed. Saving it until answer.\n",
								ast_channel_name(winner));
							ast_party_connected_line_set_init(&connected, &tmpuser->connected);
							if (!ast_connected_line_parse_data(f->data.ptr, f->datalen, &connected)) {
								ast_party_connected_line_set(&tmpuser->connected,
									&connected, NULL);
								tmpuser->pending_connected_update = 1;
							}
							ast_party_connected_line_free(&connected);
						}
						break;
					case AST_CONTROL_REDIRECTING:
						/*
						 * Ignore because we are masking the FollowMe search progress to
						 * the caller.
						 */
						break;
					case AST_CONTROL_PVT_CAUSE_CODE:
						ast_indicate_data(caller, f->subclass.integer, f->data.ptr, f->datalen);
						break;
					case -1:
						ast_verb(3, "%s stopped sounds\n", ast_channel_name(winner));
						break;
					default:
						ast_debug(1, "Dunno what to do with control type %d from %s\n",
							f->subclass.integer, ast_channel_name(winner));
						break;
					}
				} 
				if (!tpargs->enable_callee_prompt && tmpuser) {
					ast_debug(1, "Taking call with no prompt\n");
					ast_frfree(f);
					return tmpuser->ochan;
				}
				if (tmpuser && tmpuser->state == 3 && f->frametype == AST_FRAME_DTMF) {
					int cmp_len;

					if (ast_channel_stream(winner))
						ast_stopstream(winner);
					tmpuser->digts = 0;
					ast_debug(1, "DTMF received: %c\n", (char) f->subclass.integer);
					if (tmpuser->ynidx < ARRAY_LEN(tmpuser->yn) - 1) {
						tmpuser->yn[tmpuser->ynidx++] = f->subclass.integer;
					} else {
						/* Discard oldest digit. */
						memmove(tmpuser->yn, tmpuser->yn + 1,
							sizeof(tmpuser->yn) - 2 * sizeof(tmpuser->yn[0]));
						tmpuser->yn[ARRAY_LEN(tmpuser->yn) - 2] = f->subclass.integer;
					}
					ast_debug(1, "DTMF string: %s\n", tmpuser->yn);
					cmp_len = strlen(tpargs->takecall);
					if (cmp_len <= tmpuser->ynidx
						&& !strcmp(tmpuser->yn + (tmpuser->ynidx - cmp_len), tpargs->takecall)) {
						ast_debug(1, "Match to take the call!\n");
						ast_frfree(f);
						return tmpuser->ochan;
					}
					cmp_len = strlen(tpargs->nextindp);
					if (cmp_len <= tmpuser->ynidx
						&& !strcmp(tmpuser->yn + (tmpuser->ynidx - cmp_len), tpargs->nextindp)) {
						ast_debug(1, "Declined to take the call.\n");
						clear_caller(tmpuser);
					}
				}

				ast_frfree(f);
			} else {
				ast_debug(1, "we didn't get a frame. hanging up.\n");
				if (!tmpuser) {
					/* Caller hung up. */
					ast_verb(3, "The calling channel hungup. Need to drop everyone.\n");
					return NULL;
				}
				/* Outgoing channel hung up. */
				ast_channel_publish_dial(caller, winner, NULL, "NOANSWER");
				clear_caller(tmpuser);
			}
		} else {
			ast_debug(1, "timed out waiting for action\n");
		}
	}

	/* Unreachable. */
}

/*!
 * \internal
 * \brief Find an extension willing to take the call.
 *
 * \param tpargs Active Followme config.
 * \param caller Channel initiating the outgoing calls.
 *
 * \retval winner Winning outgoing call.
 * \retval NULL if could not find someone to take the call.
 */
static struct ast_channel *findmeexec(struct fm_args *tpargs, struct ast_channel *caller)
{
	struct number *nm;
	struct ast_channel *winner = NULL;
	char num[512];
	int dg, idx;
	char *rest, *number;
	struct findme_user *tmpuser;
	struct findme_user *fmuser;
	struct findme_user_listptr findme_user_list = AST_LIST_HEAD_NOLOCK_INIT_VALUE;
	struct findme_user_listptr new_user_list = AST_LIST_HEAD_NOLOCK_INIT_VALUE;

	for (idx = 1; !ast_check_hangup(caller); ++idx) {
		/* Find next followme numbers to dial. */
		AST_LIST_TRAVERSE(&tpargs->cnumbers, nm, entry) {
			if (nm->order == idx) {
				break;
			}
		}
		if (!nm) {
			ast_verb(3, "No more steps left.\n");
			break;
		}

		ast_debug(2, "Number(s) %s timeout %ld\n", nm->number, nm->timeout);

		/*
		 * Put all active outgoing channels into autoservice.
		 *
		 * This needs to be done because ast_exists_extension() may put
		 * the caller into autoservice.
		 */
		AST_LIST_TRAVERSE(&findme_user_list, tmpuser, entry) {
			if (tmpuser->ochan) {
				ast_autoservice_start(tmpuser->ochan);
			}
		}

		/* Create all new outgoing calls */
		ast_copy_string(num, nm->number, sizeof(num));
		for (number = num; number; number = rest) {
			struct ast_channel *outbound;

			rest = strchr(number, '&');
			if (rest) {
				*rest++ = 0;
			}

			/* We check if the extension exists, before creating the ast_channel struct */
			if (!ast_exists_extension(caller, tpargs->context, number, 1, S_COR(ast_channel_caller(caller)->id.number.valid, ast_channel_caller(caller)->id.number.str, NULL))) {
				ast_log(LOG_ERROR, "Extension '%s@%s' doesn't exist\n", number, tpargs->context);
				continue;
			}

			tmpuser = ast_calloc(1, sizeof(*tmpuser));
			if (!tmpuser) {
				continue;
			}

			if (ast_strlen_zero(tpargs->context)) {
				snprintf(tmpuser->dialarg, sizeof(tmpuser->dialarg), "%s%s",
					number,
					ast_test_flag(&tpargs->followmeflags, FOLLOWMEFLAG_DISABLEOPTIMIZATION)
						? "/n" : "/m");
			} else {
				snprintf(tmpuser->dialarg, sizeof(tmpuser->dialarg), "%s@%s%s",
					number, tpargs->context,
					ast_test_flag(&tpargs->followmeflags, FOLLOWMEFLAG_DISABLEOPTIMIZATION)
						? "/n" : "/m");
			}

			outbound = ast_request("Local", ast_channel_nativeformats(caller), NULL, caller,
				tmpuser->dialarg, &dg);
			if (!outbound) {
				ast_log(LOG_WARNING, "Unable to allocate a channel for Local/%s cause: %s\n",
					tmpuser->dialarg, ast_cause2str(dg));
				ast_free(tmpuser);
				continue;
			}

			ast_channel_lock_both(caller, outbound);
			ast_connected_line_copy_from_caller(ast_channel_connected(outbound), ast_channel_caller(caller));
			ast_channel_inherit_variables(caller, outbound);
			ast_channel_datastore_inherit(caller, outbound);
			ast_max_forwards_decrement(outbound);
			ast_channel_language_set(outbound, ast_channel_language(caller));
			ast_channel_req_accountcodes(outbound, caller, AST_CHANNEL_REQUESTOR_BRIDGE_PEER);
			ast_channel_musicclass_set(outbound, ast_channel_musicclass(caller));
			ast_channel_unlock(outbound);
			ast_channel_unlock(caller);

			tmpuser->ochan = outbound;
			tmpuser->state = 0;
			AST_LIST_INSERT_TAIL(&new_user_list, tmpuser, entry);
		}

		/*
		 * PREDIAL: Run gosub on all of the new callee channels
		 *
		 * We run the callee predial before ast_call() in case the user
		 * wishes to do something on the newly created channels before
		 * the channel does anything important.
		 */
		if (tpargs->predial_callee && !AST_LIST_EMPTY(&new_user_list)) {
			/* Put caller into autoservice. */
			ast_autoservice_start(caller);

			/* Run predial on all new outgoing calls. */
			AST_LIST_TRAVERSE(&new_user_list, tmpuser, entry) {
				ast_pre_call(tmpuser->ochan, tpargs->predial_callee);
			}

			/* Take caller out of autoservice. */
			if (ast_autoservice_stop(caller)) {
				/*
				 * Caller hungup.
				 *
				 * Destoy all new outgoing calls.
				 */
				while ((tmpuser = AST_LIST_REMOVE_HEAD(&new_user_list, entry))) {
					destroy_calling_node(tmpuser);
				}

				/* Take all active outgoing channels out of autoservice. */
				AST_LIST_TRAVERSE(&findme_user_list, tmpuser, entry) {
					if (tmpuser->ochan) {
						ast_autoservice_stop(tmpuser->ochan);
					}
				}
				break;
			}
		}

		/* Start all new outgoing calls */
		AST_LIST_TRAVERSE_SAFE_BEGIN(&new_user_list, tmpuser, entry) {
			ast_verb(3, "calling Local/%s\n", tmpuser->dialarg);
			if (ast_call(tmpuser->ochan, tmpuser->dialarg, 0)) {
				ast_verb(3, "couldn't reach at this number.\n");
				AST_LIST_REMOVE_CURRENT(entry);

				/* Destroy this failed new outgoing call. */
				destroy_calling_node(tmpuser);
				continue;
			}

			ast_channel_publish_dial(caller, tmpuser->ochan, tmpuser->dialarg, NULL);
		}
		AST_LIST_TRAVERSE_SAFE_END;

		/* Take all active outgoing channels out of autoservice. */
		AST_LIST_TRAVERSE_SAFE_BEGIN(&findme_user_list, tmpuser, entry) {
			if (tmpuser->ochan && ast_autoservice_stop(tmpuser->ochan)) {
				/* Existing outgoing call hungup. */
				AST_LIST_REMOVE_CURRENT(entry);
				destroy_calling_node(tmpuser);
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;

		if (AST_LIST_EMPTY(&new_user_list)) {
			/* No new channels remain at this order level.  If there were any at all. */
			continue;
		}

		/* Add new outgoing channels to the findme list. */
		AST_LIST_APPEND_LIST(&findme_user_list, &new_user_list, entry);

		winner = wait_for_winner(&findme_user_list, nm, caller, tpargs);
		if (!winner) {
			/* Remove all dead outgoing nodes. */
			AST_LIST_TRAVERSE_SAFE_BEGIN(&findme_user_list, tmpuser, entry) {
				if (!tmpuser->ochan) {
					AST_LIST_REMOVE_CURRENT(entry);
					destroy_calling_node(tmpuser);
				}
			}
			AST_LIST_TRAVERSE_SAFE_END;
			continue;
		}

		/* Destroy losing calls up to the winner.  The rest will be destroyed later. */
		while ((fmuser = AST_LIST_REMOVE_HEAD(&findme_user_list, entry))) {
			if (fmuser->ochan == winner) {
				/*
				 * Pass any connected line info up.
				 *
				 * NOTE: This code must be in line with destroy_calling_node().
				 */
				tpargs->connected_out = fmuser->connected;
				tpargs->pending_out_connected_update = fmuser->pending_connected_update;
				ast_free(fmuser);
				break;
			} else {
				/* Destroy losing call. */
				destroy_calling_node(fmuser);
			}
		}
		break;
	}
	destroy_calling_tree(&findme_user_list);
	return winner;
}

static struct call_followme *find_realtime(const char *name)
{
	struct ast_variable *var;
	struct ast_variable *v;
	struct ast_config *cfg;
	const char *catg;
	struct call_followme *new_follower;
	struct ast_str *str;

	str = ast_str_create(16);
	if (!str) {
		return NULL;
	}

	var = ast_load_realtime("followme", "name", name, SENTINEL);
	if (!var) {
		ast_free(str);
		return NULL;
	}

	if (!(new_follower = alloc_profile(name))) {
		ast_variables_destroy(var);
		ast_free(str);
		return NULL;
	}
	init_profile(new_follower, 0);

	for (v = var; v; v = v->next) {
		if (!strcasecmp(v->name, "active")) {
			if (ast_false(v->value)) {
				ast_mutex_destroy(&new_follower->lock);
				ast_free(new_follower);
				ast_variables_destroy(var);
				ast_free(str);
				return NULL;
			}
		} else {
			profile_set_param(new_follower, v->name, v->value, 0, 0);
		}
	}

	ast_variables_destroy(var);
	new_follower->realtime = 1;

	/* Load numbers */
	cfg = ast_load_realtime_multientry("followme_numbers", "ordinal LIKE", "%", "name",
		name, SENTINEL);
	if (!cfg) {
		ast_mutex_destroy(&new_follower->lock);
		ast_free(new_follower);
		ast_free(str);
		return NULL;
	}

	for (catg = ast_category_browse(cfg, NULL); catg; catg = ast_category_browse(cfg, catg)) {
		const char *numstr;
		const char *timeoutstr;
		const char *ordstr;
		int timeout;
		struct number *cur;

		if (!(numstr = ast_variable_retrieve(cfg, catg, "phonenumber"))) {
			continue;
		}
		if (!(timeoutstr = ast_variable_retrieve(cfg, catg, "timeout"))
			|| sscanf(timeoutstr, "%30d", &timeout) != 1
			|| timeout < 1) {
			timeout = 25;
		}
		/* This one has to exist; it was part of the query */
		ordstr = ast_variable_retrieve(cfg, catg, "ordinal");
		ast_str_set(&str, 0, "%s", numstr);
		if ((cur = create_followme_number(ast_str_buffer(str), timeout, atoi(ordstr)))) {
			AST_LIST_INSERT_TAIL(&new_follower->numbers, cur, entry);
		}
	}
	ast_config_destroy(cfg);

	ast_free(str);
	return new_follower;
}

static void end_bridge_callback(void *data)
{
	char buf[80];
	time_t end;
	struct ast_channel *chan = data;

	time(&end);

	ast_channel_lock(chan);
	snprintf(buf, sizeof(buf), "%d", ast_channel_get_up_time(chan));
	pbx_builtin_setvar_helper(chan, "ANSWEREDTIME", buf);
	snprintf(buf, sizeof(buf), "%d", ast_channel_get_duration(chan));
	pbx_builtin_setvar_helper(chan, "DIALEDTIME", buf);
	ast_channel_unlock(chan);
}

static void end_bridge_callback_data_fixup(struct ast_bridge_config *bconfig, struct ast_channel *originator, struct ast_channel *terminator)
{
	bconfig->end_bridge_callback_data = originator;
}

static int app_exec(struct ast_channel *chan, const char *data)
{
	struct fm_args *targs;
	struct ast_bridge_config config;
	struct call_followme *f;
	struct number *nm, *newnm;
	int res = 0;
	char *argstr;
	struct ast_channel *caller;
	struct ast_channel *outbound;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(followmeid);
		AST_APP_ARG(options);
	);
	char *opt_args[FOLLOWMEFLAG_ARG_ARRAY_SIZE];
	int max_forwards;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument (followmeid)\n", app);
		return -1;
	}

	ast_channel_lock(chan);
	max_forwards = ast_max_forwards_get(chan);
	ast_channel_unlock(chan);

	if (max_forwards <= 0) {
		ast_log(LOG_WARNING, "Unable to execute FollowMe on channel %s. Max forwards exceeded\n",
				ast_channel_name(chan));
		return -1;
	}

	argstr = ast_strdupa((char *) data);

	AST_STANDARD_APP_ARGS(args, argstr);

	if (ast_strlen_zero(args.followmeid)) {
		ast_log(LOG_WARNING, "%s requires an argument (followmeid)\n", app);
		return -1;
	}

	targs = ast_calloc(1, sizeof(*targs));
	if (!targs) {
		return -1;
	}

	AST_RWLIST_RDLOCK(&followmes);
	AST_RWLIST_TRAVERSE(&followmes, f, entry) {
		if (!strcasecmp(f->name, args.followmeid) && (f->active))
			break;
	}
	AST_RWLIST_UNLOCK(&followmes);

	ast_debug(1, "New profile %s.\n", args.followmeid);

	if (!f) {
		f = find_realtime(args.followmeid);
	}

	if (!f) {
		ast_log(LOG_WARNING, "Profile requested, %s, not found in the configuration.\n", args.followmeid);
		ast_free(targs);
		return 0;
	}

	/* XXX TODO: Reinsert the db check value to see whether or not follow-me is on or off */
	if (args.options) {
		ast_app_parse_options(followme_opts, &targs->followmeflags, opt_args, args.options);
	}

	/* Lock the profile lock and copy out everything we need to run with before unlocking it again */
	ast_mutex_lock(&f->lock);
	targs->enable_callee_prompt = f->enable_callee_prompt;
	targs->mohclass = ast_strdupa(f->moh);
	ast_copy_string(targs->context, f->context, sizeof(targs->context));
	ast_copy_string(targs->takecall, f->takecall, sizeof(targs->takecall));
	ast_copy_string(targs->nextindp, f->nextindp, sizeof(targs->nextindp));
	ast_copy_string(targs->callfromprompt, f->callfromprompt, sizeof(targs->callfromprompt));
	ast_copy_string(targs->norecordingprompt, f->norecordingprompt, sizeof(targs->norecordingprompt));
	ast_copy_string(targs->optionsprompt, f->optionsprompt, sizeof(targs->optionsprompt));
	ast_copy_string(targs->plsholdprompt, f->plsholdprompt, sizeof(targs->plsholdprompt));
	ast_copy_string(targs->statusprompt, f->statusprompt, sizeof(targs->statusprompt));
	ast_copy_string(targs->sorryprompt, f->sorryprompt, sizeof(targs->sorryprompt));
	/* Copy the numbers we're going to use into another list in case the master list should get modified 
	   (and locked) while we're trying to do a follow-me */
	AST_LIST_HEAD_INIT_NOLOCK(&targs->cnumbers);
	AST_LIST_TRAVERSE(&f->numbers, nm, entry) {
		newnm = create_followme_number(nm->number, nm->timeout, nm->order);
		if (newnm) {
			AST_LIST_INSERT_TAIL(&targs->cnumbers, newnm, entry);
		}
	}
	ast_mutex_unlock(&f->lock);

	/* PREDIAL: Preprocess any callee gosub arguments. */
	if (ast_test_flag(&targs->followmeflags, FOLLOWMEFLAG_PREDIAL_CALLEE)
		&& !ast_strlen_zero(opt_args[FOLLOWMEFLAG_ARG_PREDIAL_CALLEE])) {
		ast_replace_subargument_delimiter(opt_args[FOLLOWMEFLAG_ARG_PREDIAL_CALLEE]);
		targs->predial_callee =
			ast_app_expand_sub_args(chan, opt_args[FOLLOWMEFLAG_ARG_PREDIAL_CALLEE]);
	}

	/* PREDIAL: Run gosub on the caller's channel */
	if (ast_test_flag(&targs->followmeflags, FOLLOWMEFLAG_PREDIAL_CALLER)
		&& !ast_strlen_zero(opt_args[FOLLOWMEFLAG_ARG_PREDIAL_CALLER])) {
		ast_replace_subargument_delimiter(opt_args[FOLLOWMEFLAG_ARG_PREDIAL_CALLER]);
		ast_app_exec_sub(NULL, chan, opt_args[FOLLOWMEFLAG_ARG_PREDIAL_CALLER], 0);
	}

	/* Forget the 'N' option if the call is already up. */
	if (ast_channel_state(chan) == AST_STATE_UP) {
		ast_clear_flag(&targs->followmeflags, FOLLOWMEFLAG_NOANSWER);
	}

	if (ast_test_flag(&targs->followmeflags, FOLLOWMEFLAG_NOANSWER)) {
		ast_indicate(chan, AST_CONTROL_RINGING);
	} else {
		/* Answer the call */
		if (ast_channel_state(chan) != AST_STATE_UP) {
			ast_answer(chan);
		}

		if (ast_test_flag(&targs->followmeflags, FOLLOWMEFLAG_STATUSMSG)) {
			ast_stream_and_wait(chan, targs->statusprompt, "");
		}

		if (ast_test_flag(&targs->followmeflags, FOLLOWMEFLAG_RECORDNAME)) {
			int duration = 5;

			snprintf(targs->namerecloc, sizeof(targs->namerecloc), "%s/followme.%s",
				ast_config_AST_SPOOL_DIR, ast_channel_uniqueid(chan));
			if (ast_play_and_record(chan, "vm-rec-name", targs->namerecloc, 5, REC_FORMAT, &duration,
				NULL, ast_dsp_get_threshold_from_settings(THRESHOLD_SILENCE), 0, NULL) < 0) {
				goto outrun;
			}
			if (!ast_fileexists(targs->namerecloc, NULL, ast_channel_language(chan))) {
				targs->namerecloc[0] = '\0';
			}
		}

		if (!ast_test_flag(&targs->followmeflags, FOLLOWMEFLAG_DISABLEHOLDPROMPT)) {
			if (ast_streamfile(chan, targs->plsholdprompt, ast_channel_language(chan))) {
				goto outrun;
			}
			if (ast_waitstream(chan, "") < 0)
				goto outrun;
		}
		ast_moh_start(chan, targs->mohclass, NULL);
	}

	ast_channel_lock(chan);
	ast_connected_line_copy_from_caller(&targs->connected_in, ast_channel_caller(chan));
	ast_channel_unlock(chan);

	outbound = findmeexec(targs, chan);
	if (!outbound) {
		if (ast_test_flag(&targs->followmeflags, FOLLOWMEFLAG_NOANSWER)) {
			if (ast_channel_state(chan) != AST_STATE_UP) {
				ast_answer(chan);
			}
		} else {
			ast_moh_stop(chan);
		}

		if (ast_test_flag(&targs->followmeflags, FOLLOWMEFLAG_UNREACHABLEMSG)) {
			ast_stream_and_wait(chan, targs->sorryprompt, "");
		}
		res = 0;
	} else {
		caller = chan;
		/* Bridge the two channels. */

		memset(&config, 0, sizeof(config));
		ast_set_flag(&(config.features_callee), AST_FEATURE_REDIRECT);
		ast_set_flag(&(config.features_callee), AST_FEATURE_AUTOMON);
		ast_set_flag(&(config.features_caller), AST_FEATURE_AUTOMON);
		config.end_bridge_callback = end_bridge_callback;
		config.end_bridge_callback_data = chan;
		config.end_bridge_callback_data_fixup = end_bridge_callback_data_fixup;

		/* Update connected line to caller if available. */
		if (targs->pending_out_connected_update) {
			if (ast_channel_connected_line_sub(outbound, caller, &targs->connected_out, 0) &&
				ast_channel_connected_line_macro(outbound, caller, &targs->connected_out, 1, 0)) {
				ast_channel_update_connected_line(caller, &targs->connected_out, NULL);
			}
		}

		if (ast_test_flag(&targs->followmeflags, FOLLOWMEFLAG_NOANSWER)) {
			if (ast_channel_state(caller) != AST_STATE_UP) {
				ast_answer(caller);
			}
		} else {
			ast_moh_stop(caller);
		}

		/* Be sure no generators are left on it */
		ast_deactivate_generator(caller);
		/* Make sure channels are compatible */
		res = ast_channel_make_compatible(caller, outbound);
		if (res < 0) {
			ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", ast_channel_name(caller), ast_channel_name(outbound));
			ast_autoservice_chan_hangup_peer(caller, outbound);
			goto outrun;
		}

		/* Update connected line to winner if changed. */
		if (targs->pending_in_connected_update) {
			if (ast_channel_connected_line_sub(caller, outbound, &targs->connected_in, 0) &&
				ast_channel_connected_line_macro(caller, outbound, &targs->connected_in, 0, 0)) {
				ast_channel_update_connected_line(outbound, &targs->connected_in, NULL);
			}
		}

		/* Put winner on hold if caller requested. */
		if (targs->pending_hold) {
			if (ast_strlen_zero(targs->suggested_moh)) {
				ast_indicate_data(outbound, AST_CONTROL_HOLD, NULL, 0);
			} else {
				ast_indicate_data(outbound, AST_CONTROL_HOLD,
					targs->suggested_moh, strlen(targs->suggested_moh) + 1);
			}
		}

		res = ast_bridge_call(caller, outbound, &config);
	}

outrun:
	while ((nm = AST_LIST_REMOVE_HEAD(&targs->cnumbers, entry))) {
		ast_free(nm);
	}
	if (!ast_strlen_zero(targs->namerecloc)) {
		int ret;
		char fn[PATH_MAX + sizeof(REC_FORMAT)];

		snprintf(fn, sizeof(fn), "%s.%s", targs->namerecloc,
			     REC_FORMAT);
		ret = unlink(fn);
		if (ret != 0) {
			ast_log(LOG_NOTICE, "Failed to delete recorded name file %s: %d (%s)\n",
					fn, errno, strerror(errno));
		} else {
			ast_debug(2, "deleted recorded prompt %s.\n", fn);
		}
	}
	ast_free((char *) targs->predial_callee);
	ast_party_connected_line_free(&targs->connected_in);
	ast_party_connected_line_free(&targs->connected_out);
	ast_free(targs);

	if (f->realtime) {
		/* Not in list */
		free_numbers(f);
		ast_free(f);
	}

	return res;
}

static int unload_module(void)
{
	struct call_followme *f;

	ast_unregister_application(app);

	/* Free Memory. Yeah! I'm free! */
	AST_RWLIST_WRLOCK(&followmes);
	while ((f = AST_RWLIST_REMOVE_HEAD(&followmes, entry))) {
		free_numbers(f);
		ast_free(f);
	}

	AST_RWLIST_UNLOCK(&followmes);

	return 0;
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the 
 * configuration file or other non-critical problem return 
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	if(!reload_followme(0))
		return AST_MODULE_LOAD_DECLINE;

	return ast_register_application_xml(app, app_exec);
}

static int reload(void)
{
	reload_followme(1);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Find-Me/Follow-Me Application",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
);
