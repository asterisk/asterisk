/*
 * Asterisk -- A telephony toolkit for Linux.
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
 * \arg See \ref Config_followme
 *
 * \ingroup applications
 */

/*** MODULEINFO
        <depend>chan_local</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/say.h"
#include "asterisk/features.h"
#include "asterisk/musiconhold.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/config.h"
#include "asterisk/monitor.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/astdb.h"
#include "asterisk/app.h"

static char *app = "FollowMe";
static char *synopsis = "Find-Me/Follow-Me application";
static char *descrip = 
"  FollowMe(followmeid|options):\n"
"This application performs Find-Me/Follow-Me functionality for the caller\n"
"as defined in the profile matching the <followmeid> parameter in\n"
"followme.conf. If the specified <followmeid> profile doesn't exist in\n"
"followme.conf, execution will be returned to the dialplan and call\n"
"execution will continue at the next priority.\n\n"
"  Options:\n"
"    s    - Playback the incoming status message prior to starting the follow-me step(s)\n"
"    a    - Record the caller's name so it can be announced to the callee on each step\n" 
"    n    - Playback the unreachable status message if we've run out of steps to reach the\n"
"           or the callee has elected not to be reachable.\n"
"Returns -1 on hangup\n";

/*! \brief Number structure */
struct number {
	char number[512];	/*!< Phone Number(s) and/or Extension(s) */
	long timeout;		/*!< Dial Timeout, if used. */
	char language[MAX_LANGUAGE]; /*!< The language to be used on this dial, if used. */
	int order;		/*!< The order to dial in */
	AST_LIST_ENTRY(number) entry; /*!< Next Number record */
};

/*! \brief Data structure for followme scripts */
struct call_followme {
	ast_mutex_t lock;
	char name[AST_MAX_EXTENSION];	/*!< Name - FollowMeID */
	char moh[AST_MAX_CONTEXT];	/*!< Music On Hold Class to be used */
	char context[AST_MAX_CONTEXT];  /*!< Context to dial from */
	unsigned int active;		/*!< Profile is active (1), or disabled (0). */
	char takecall[20];		/*!< Digit mapping to take a call */
	char nextindp[20];		/*!< Digit mapping to decline a call */
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
	struct ast_channel *chan;
	char *mohclass;
	AST_LIST_HEAD_NOLOCK(cnumbers, number) cnumbers;
	int status;
	char context[AST_MAX_CONTEXT];
	char namerecloc[AST_MAX_CONTEXT];
	struct ast_channel *outbound;
	char takecall[20];		/*!< Digit mapping to take a call */
	char nextindp[20];		/*!< Digit mapping to decline a call */
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
	int state;
	char dialarg[256];
	char yn[10];
	int ynidx; 
	long digts;
	int cleared;
	AST_LIST_ENTRY(findme_user) entry;	
};

enum {
	FOLLOWMEFLAG_STATUSMSG = (1 << 0),
	FOLLOWMEFLAG_RECORDNAME = (1 << 1),
	FOLLOWMEFLAG_UNREACHABLEMSG = (1 << 2)
};

AST_APP_OPTIONS(followme_opts, {
	AST_APP_OPTION('s', FOLLOWMEFLAG_STATUSMSG ),
	AST_APP_OPTION('a', FOLLOWMEFLAG_RECORDNAME ),
	AST_APP_OPTION('n', FOLLOWMEFLAG_UNREACHABLEMSG ),
});

static int ynlongest = 0;

static const char *featuredigittostr;
static int featuredigittimeout = 5000;		/*!< Feature Digit Timeout */
static const char *defaultmoh = "default";    	/*!< Default Music-On-Hold Class */

static char takecall[20] = "1", nextindp[20] = "2";
static char callfromprompt[PATH_MAX] = "followme/call-from";
static char norecordingprompt[PATH_MAX] = "followme/no-recording";
static char optionsprompt[PATH_MAX] = "followme/options";
static char plsholdprompt[PATH_MAX] = "followme/pls-hold-while-try";
static char statusprompt[PATH_MAX] = "followme/status";
static char sorryprompt[PATH_MAX] = "followme/sorry";


static AST_LIST_HEAD_STATIC(followmes, call_followme);
AST_LIST_HEAD_NOLOCK(findme_user_listptr, findme_user);

static void free_numbers(struct call_followme *f)
{
	/* Free numbers attached to the profile */
	struct number *prev;

	while ((prev = AST_LIST_REMOVE_HEAD(&f->numbers, entry)))
		/* Free the number */
		free(prev);
	AST_LIST_HEAD_INIT_NOLOCK(&f->numbers);

	while ((prev = AST_LIST_REMOVE_HEAD(&f->blnumbers, entry)))
		/* Free the blacklisted number */
		free(prev);
	AST_LIST_HEAD_INIT_NOLOCK(&f->blnumbers);

	while ((prev = AST_LIST_REMOVE_HEAD(&f->wlnumbers, entry)))
		/* Free the whitelisted number */
		free(prev);
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
	f->moh[0] = '\0';
	f->context[0] = '\0';
	ast_copy_string(f->takecall, takecall, sizeof(f->takecall));
	ast_copy_string(f->nextindp, nextindp, sizeof(f->nextindp));
	ast_copy_string(f->callfromprompt, callfromprompt, sizeof(f->callfromprompt));
	ast_copy_string(f->norecordingprompt, norecordingprompt, sizeof(f->norecordingprompt));
	ast_copy_string(f->optionsprompt, optionsprompt, sizeof(f->optionsprompt));
	ast_copy_string(f->plsholdprompt, plsholdprompt, sizeof(f->plsholdprompt));
	ast_copy_string(f->statusprompt, statusprompt, sizeof(f->statusprompt));
	ast_copy_string(f->sorryprompt, sorryprompt, sizeof(f->sorryprompt));
	AST_LIST_HEAD_INIT_NOLOCK(&f->numbers);
	AST_LIST_HEAD_INIT_NOLOCK(&f->blnumbers);
	AST_LIST_HEAD_INIT_NOLOCK(&f->wlnumbers);
	return f;
}

static void init_profile(struct call_followme *f)
{
	f->active = 1;
	ast_copy_string(f->moh, defaultmoh, sizeof(f->moh));
}

   
   
/*! \brief Set parameter in profile from configuration file */
static void profile_set_param(struct call_followme *f, const char *param, const char *val, int linenum, int failunknown)
{

	if (!strcasecmp(param, "musicclass") || !strcasecmp(param, "musiconhold") || !strcasecmp(param, "music")) 
		ast_copy_string(f->moh, val, sizeof(f->moh));
	else if (!strcasecmp(param, "context")) 
		ast_copy_string(f->context, val, sizeof(f->context));
	else if (!strcasecmp(param, "takecall"))
		ast_copy_string(f->takecall, val, sizeof(f->takecall));
	else if (!strcasecmp(param, "declinecall"))
		ast_copy_string(f->nextindp, val, sizeof(f->nextindp));
	else if (!strcasecmp(param, "call-from-prompt"))
		ast_copy_string(f->callfromprompt, val, sizeof(f->callfromprompt));
	else if (!strcasecmp(param, "followme-norecording-prompt")) 
		ast_copy_string(f->norecordingprompt, val, sizeof(f->norecordingprompt));
	else if (!strcasecmp(param, "followme-options-prompt")) 
		ast_copy_string(f->optionsprompt, val, sizeof(f->optionsprompt));
	else if (!strcasecmp(param, "followme-pls-hold-prompt"))
		ast_copy_string(f->plsholdprompt, val, sizeof(f->plsholdprompt));
	else if (!strcasecmp(param, "followme-status-prompt")) 
		ast_copy_string(f->statusprompt, val, sizeof(f->statusprompt));
	else if (!strcasecmp(param, "followme-sorry-prompt")) 
		ast_copy_string(f->sorryprompt, val, sizeof(f->sorryprompt));
	else if (failunknown) {
		if (linenum >= 0)
			ast_log(LOG_WARNING, "Unknown keyword in profile '%s': %s at line %d of followme.conf\n", f->name, param, linenum);
		else
			ast_log(LOG_WARNING, "Unknown keyword in profile '%s': %s\n", f->name, param);
	}
}

/*! \brief Add a new number */
static struct number *create_followme_number(char *number, char *language, int timeout, int numorder)
{
	struct number *cur;
	char *tmp;
	

	if (!(cur = ast_calloc(1, sizeof(*cur))))
		return NULL;

	cur->timeout = timeout;
	if ((tmp = strchr(number, ','))) 
		*tmp = '\0';
	ast_copy_string(cur->number, number, sizeof(cur->number));
	ast_copy_string(cur->language, language, sizeof(cur->language));
	cur->order = numorder;
	if (option_debug)
		ast_log(LOG_DEBUG, "Created a number, %s, order of , %d, with a timeout of %ld.\n", cur->number, cur->order, cur->timeout);

	return cur;
}

/*! \brief Reload followme application module */
static int reload_followme(void)
{
	struct call_followme *f;
	struct ast_config *cfg;
	char *cat = NULL, *tmp;
	struct ast_variable *var;
	struct number *cur, *nm;
	int new, idx;
	char numberstr[90];
	int timeout;
	char *timeoutstr;
	int numorder;	
	const char *takecallstr;
	const char *declinecallstr;
	const char *tmpstr;

	cfg = ast_config_load("followme.conf");
	if (!cfg) {
		ast_log(LOG_WARNING, "No follow me config file (followme.conf), so no follow me\n");
		return 0;
	}

	AST_LIST_LOCK(&followmes);

	/* Reset Global Var Values */
	featuredigittimeout = 5000;

	/* Mark all profiles as inactive for the moment */
	AST_LIST_TRAVERSE(&followmes, f, entry) {
		f->active = 0;
	}
	featuredigittostr = ast_variable_retrieve(cfg, "general", "featuredigittimeout");
	
	if (!ast_strlen_zero(featuredigittostr)) {
		if (!sscanf(featuredigittostr, "%30d", &featuredigittimeout))
			featuredigittimeout = 5000;
	}

	takecallstr = ast_variable_retrieve(cfg, "general", "takecall");
	if (!ast_strlen_zero(takecallstr))
		ast_copy_string(takecall, takecallstr, sizeof(takecall));
	
	declinecallstr = ast_variable_retrieve(cfg, "general", "declinecall");
	if (!ast_strlen_zero(declinecallstr))
		ast_copy_string(nextindp, declinecallstr, sizeof(nextindp));

	tmpstr = ast_variable_retrieve(cfg, "general", "call-from-prompt");
	if (!ast_strlen_zero(tmpstr))
		ast_copy_string(callfromprompt, tmpstr, sizeof(callfromprompt));

	tmpstr = ast_variable_retrieve(cfg, "general", "norecording-prompt");
	if (!ast_strlen_zero(tmpstr))
		ast_copy_string(norecordingprompt, tmpstr, sizeof(norecordingprompt));

	tmpstr = ast_variable_retrieve(cfg, "general", "options-prompt");
	if (!ast_strlen_zero(tmpstr))
		ast_copy_string(optionsprompt, tmpstr, sizeof(optionsprompt));

	tmpstr = ast_variable_retrieve(cfg, "general", "pls-hold-prompt");
	if (!ast_strlen_zero(tmpstr))
		ast_copy_string(plsholdprompt, tmpstr, sizeof(plsholdprompt));

	tmpstr = ast_variable_retrieve(cfg, "general", "status-prompt");
	if (!ast_strlen_zero(tmpstr))
		ast_copy_string(statusprompt, tmpstr, sizeof(statusprompt));

	tmpstr = ast_variable_retrieve(cfg, "general", "sorry-prompt");
	if (!ast_strlen_zero(tmpstr))
		ast_copy_string(sorryprompt, tmpstr, sizeof(sorryprompt));

	/* Chug through config file */
	while ((cat = ast_category_browse(cfg, cat))) {
		if (!strcasecmp(cat, "general"))
			continue;
		/* Define a new profile */
		/* Look for an existing one */
		AST_LIST_TRAVERSE(&followmes, f, entry) {
			if (!strcasecmp(f->name, cat))
				break;
		}
		if (option_debug)
			ast_log(LOG_DEBUG, "New profile %s.\n", cat);
		if (!f) {
			/* Make one then */
			f = alloc_profile(cat);
			new = 1;
		} else
			new = 0;
	
		if (f) {
			if (!new)
				ast_mutex_lock(&f->lock);
			/* Re-initialize the profile */
			init_profile(f);
			free_numbers(f);
			var = ast_variable_browse(cfg, cat);
			while(var) {
				if (!strcasecmp(var->name, "number")) {
					/* Add a new number */
					ast_copy_string(numberstr, var->value, sizeof(numberstr));
					if ((tmp = strchr(numberstr, ','))) {
						*tmp = '\0';
						tmp++;
						timeoutstr = ast_strdupa(tmp);
						if ((tmp = strchr(timeoutstr, ','))) {
							*tmp = '\0';
							tmp++;
							numorder = atoi(tmp);
							if (numorder < 0)
								numorder = 0;
						} else 
							numorder = 0;
						timeout = atoi(timeoutstr);
						if (timeout < 0) 
							timeout = 25;
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
					cur = create_followme_number(numberstr, "", timeout, numorder);
					AST_LIST_INSERT_TAIL(&f->numbers, cur, entry);
				} else {
					profile_set_param(f, var->name, var->value, var->lineno, 1);
					if (option_debug > 1)
						ast_log(LOG_DEBUG, "Logging parameter %s with value %s from lineno %d\n", var->name, var->value, var->lineno);
				}
				var = var->next;
			} /* End while(var) loop */

			if (!new) 
				ast_mutex_unlock(&f->lock);
			else
				AST_LIST_INSERT_HEAD(&followmes, f, entry);
		}
	}
	ast_config_destroy(cfg);

	AST_LIST_UNLOCK(&followmes);

	return 1;
}

static void clear_caller(struct findme_user *tmpuser)
{
	struct ast_channel *outbound;
	
	if (tmpuser && tmpuser->ochan && tmpuser->state >= 0) {
		outbound = tmpuser->ochan;
		if (!outbound->cdr) {
			outbound->cdr = ast_cdr_alloc();
			if (outbound->cdr)
				ast_cdr_init(outbound->cdr, outbound);
		}
		if (outbound->cdr) {
			char tmp[256];

			snprintf(tmp, sizeof(tmp), "%s/%s", "Local", tmpuser->dialarg);
			ast_cdr_setapp(outbound->cdr, "FollowMe", tmp);
			ast_cdr_update(outbound);
			ast_cdr_start(outbound->cdr);
			ast_cdr_end(outbound->cdr);
			/* If the cause wasn't handled properly */
			if (ast_cdr_disposition(outbound->cdr, outbound->hangupcause))
				ast_cdr_failed(outbound->cdr);
		} else
			ast_log(LOG_WARNING, "Unable to create Call Detail Record\n");
		ast_hangup(tmpuser->ochan);
	}

}

static void clear_calling_tree(struct findme_user_listptr *findme_user_list) 
{
	struct findme_user *tmpuser;
	
	AST_LIST_TRAVERSE(findme_user_list, tmpuser, entry) {
		clear_caller(tmpuser);
		tmpuser->cleared = 1;
	}
	
}



static struct ast_channel *wait_for_winner(struct findme_user_listptr *findme_user_list, struct number *nm, struct ast_channel *caller, char *namerecloc, int *status, struct fm_args *tpargs) 
{
	struct ast_channel *watchers[256];
	int pos;
	struct ast_channel *winner;
	struct ast_frame *f;
	int ctstatus;
	int dg;
	struct findme_user *tmpuser;
	int to = 0;
	int livechannels = 0;
	int tmpto;
	long totalwait = 0, wtd, towas = 0;
	char *callfromname;
	char *pressbuttonname;

	/* ------------ wait_for_winner_channel start --------------- */ 

	callfromname = ast_strdupa(tpargs->callfromprompt);
	pressbuttonname = ast_strdupa(tpargs->optionsprompt);	

	if (!AST_LIST_EMPTY(findme_user_list)) {
		if (!caller) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Original caller hungup. Cleanup.\n");
			clear_calling_tree(findme_user_list);
			return NULL;
		}
		ctstatus = 0;
		totalwait = nm->timeout * 1000;
		wtd = 0;
		while (!ctstatus) {
			to = 1000;
			pos = 1; 
			livechannels = 0;
			watchers[0] = caller;
		
			dg = 0;	
			winner = NULL;	
			AST_LIST_TRAVERSE(findme_user_list, tmpuser, entry) {
				if (tmpuser->state >= 0 && tmpuser->ochan) {
					if (tmpuser->state == 3) 
						tmpuser->digts += (towas - wtd);
					if (tmpuser->digts && (tmpuser->digts > featuredigittimeout)) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "We've been waiting for digits longer than we should have.\n");
						if (!ast_strlen_zero(namerecloc)) {
							tmpuser->state = 1;
							tmpuser->digts = 0;
							if (!ast_streamfile(tmpuser->ochan, callfromname, tmpuser->ochan->language)) {
								ast_sched_runq(tmpuser->ochan->sched);
							} else {
								ast_log(LOG_WARNING, "Unable to playback %s.\n", callfromname);
								return NULL;
							}							
						} else {
							tmpuser->state = 2;
							tmpuser->digts = 0;
							if (!ast_streamfile(tmpuser->ochan, tpargs->norecordingprompt, tmpuser->ochan->language))
								ast_sched_runq(tmpuser->ochan->sched);
							else {
								ast_log(LOG_WARNING, "Unable to playback %s.\n", tpargs->norecordingprompt);
								return NULL;
							}
						}
					}
					if (tmpuser->ochan->stream) {
						ast_sched_runq(tmpuser->ochan->sched);
						tmpto = ast_sched_wait(tmpuser->ochan->sched);
						if (tmpto > 0 && tmpto < to)
							to = tmpto;
						else if (tmpto < 0 && !tmpuser->ochan->timingfunc) {
							ast_stopstream(tmpuser->ochan);
							if (tmpuser->state == 1) {
								if (option_verbose > 2)
									ast_verbose(VERBOSE_PREFIX_3 "Playback of the call-from file appears to be done.\n");
								if (!ast_streamfile(tmpuser->ochan, namerecloc, tmpuser->ochan->language)) {
									tmpuser->state = 2;
								} else {
									ast_log(LOG_NOTICE, "Unable to playback %s. Maybe the caller didn't record their name?\n", namerecloc);
									memset(tmpuser->yn, 0, sizeof(tmpuser->yn));
									tmpuser->ynidx = 0;
									if (!ast_streamfile(tmpuser->ochan, pressbuttonname, tmpuser->ochan->language))
										tmpuser->state = 3;
									else {
										ast_log(LOG_WARNING, "Unable to playback %s.\n", pressbuttonname);
										return NULL;
									} 
								}
							} else if (tmpuser->state == 2) {
								if (option_verbose > 2)
									ast_verbose(VERBOSE_PREFIX_3 "Playback of name file appears to be done.\n");
								memset(tmpuser->yn, 0, sizeof(tmpuser->yn));
								tmpuser->ynidx = 0;
								if (!ast_streamfile(tmpuser->ochan, pressbuttonname, tmpuser->ochan->language)) {
									tmpuser->state = 3;
									
								} else {
									return NULL;
								} 
							} else if (tmpuser->state == 3) {
								if (option_verbose > 2)
									ast_verbose(VERBOSE_PREFIX_3 "Playback of the next step file appears to be done.\n");
								tmpuser->digts = 0;
							}
						}
					}
					watchers[pos++] = tmpuser->ochan;
					livechannels++;
				}
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
				if (option_verbose > 2)	
					ast_verbose(VERBOSE_PREFIX_3 "We've hit our timeout for this step. Drop everyone and move on to the next one. %ld\n", totalwait);
				clear_calling_tree(findme_user_list);
				return NULL;
			}
			if (winner) {
				/* Need to find out which channel this is */
				dg = 0;
				while ((winner != watchers[dg]) && (dg < 256))
					dg++;
				AST_LIST_TRAVERSE(findme_user_list, tmpuser, entry)
					if (tmpuser->ochan == winner)
						break;
				f = ast_read(winner);
				if (f) {
					if (f->frametype == AST_FRAME_CONTROL) {
						switch(f->subclass) {
						case AST_CONTROL_HANGUP:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s received a hangup frame.\n", winner->name);
							if (dg == 0) {
								if (option_verbose > 2)
									ast_verbose( VERBOSE_PREFIX_3 "The calling channel hungup. Need to drop everyone else.\n");
								clear_calling_tree(findme_user_list);
								ctstatus = -1;
							}
							break;
						case AST_CONTROL_ANSWER:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", winner->name, caller->name);
							/* If call has been answered, then the eventual hangup is likely to be normal hangup */ 
							winner->hangupcause = AST_CAUSE_NORMAL_CLEARING;
							caller->hangupcause = AST_CAUSE_NORMAL_CLEARING;
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "Starting playback of %s\n", callfromname);
							if (dg > 0) {
								if (!ast_strlen_zero(namerecloc)) {
									if (!ast_streamfile(winner, callfromname, winner->language)) {
										ast_sched_runq(winner->sched);
										tmpuser->state = 1;
									} else {
										ast_log(LOG_WARNING, "Unable to playback %s.\n", callfromname);
										ast_frfree(f);
										return NULL;
									}				
								} else {			
									tmpuser->state = 2;
									if (!ast_streamfile(tmpuser->ochan, tpargs->norecordingprompt, tmpuser->ochan->language))
										ast_sched_runq(tmpuser->ochan->sched);
									else {
										ast_log(LOG_WARNING, "Unable to playback %s.\n", tpargs->norecordingprompt);
										ast_frfree(f);
										return NULL;
									}
								}
							}
							break;
						case AST_CONTROL_BUSY:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is busy\n", winner->name);
							break;
						case AST_CONTROL_CONGESTION:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is circuit-busy\n", winner->name);
							break;
						case AST_CONTROL_RINGING:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is ringing\n", winner->name);
							break;
						case AST_CONTROL_PROGRESS:
							if (option_verbose > 2)
								ast_verbose ( VERBOSE_PREFIX_3 "%s is making progress passing it to %s\n", winner->name, caller->name);
							break;
						case AST_CONTROL_VIDUPDATE:
							if (option_verbose > 2)
								ast_verbose ( VERBOSE_PREFIX_3 "%s requested a video update, passing it to %s\n", winner->name, caller->name);
							break;
						case AST_CONTROL_SRCUPDATE:
							if (option_verbose > 2)
								ast_verbose ( VERBOSE_PREFIX_3 "%s requested a source update, passing it to %s\n", winner->name, caller->name);
							break;
						case AST_CONTROL_PROCEEDING:
							if (option_verbose > 2)
								ast_verbose ( VERBOSE_PREFIX_3 "%s is proceeding passing it to %s\n", winner->name,caller->name);
							break;
						case AST_CONTROL_HOLD:
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Call on %s placed on hold\n", winner->name);
							break;
						case AST_CONTROL_UNHOLD:
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Call on %s left from hold\n", winner->name);
							break;
						case AST_CONTROL_OFFHOOK:
						case AST_CONTROL_FLASH:
							/* Ignore going off hook and flash */
							break;
						case -1:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s stopped sounds\n", winner->name);
							break;
						default:
							if (option_debug)
								ast_log(LOG_DEBUG, "Dunno what to do with control type %d\n", f->subclass);
							break;
						}
					} 
					if (tmpuser && tmpuser->state == 3 && f->frametype == AST_FRAME_DTMF) {
						if (winner->stream)
							ast_stopstream(winner);
						tmpuser->digts = 0;
						if (option_debug)
							ast_log(LOG_DEBUG, "DTMF received: %c\n",(char) f->subclass);
						tmpuser->yn[tmpuser->ynidx] = (char) f->subclass;
						tmpuser->ynidx++;
						if (option_debug)
							ast_log(LOG_DEBUG, "DTMF string: %s\n", tmpuser->yn);
						if (tmpuser->ynidx >= ynlongest) {
							if (option_debug)
								ast_log(LOG_DEBUG, "reached longest possible match - doing evals\n");
							if (!strcmp(tmpuser->yn, tpargs->takecall)) {
								if (option_debug)
									ast_log(LOG_DEBUG, "Match to take the call!\n");
								ast_frfree(f);
								return tmpuser->ochan;	
							}
							if (!strcmp(tmpuser->yn, tpargs->nextindp)) {
								if (option_debug)
									ast_log(LOG_DEBUG, "Next in dial plan step requested.\n");
								*status = 1;
								ast_frfree(f);
								return NULL;
							}	

						}
					}
					
					ast_frfree(f);
				} else {
					if (winner) {
						if (option_debug)
							ast_log(LOG_DEBUG, "we didn't get a frame. hanging up. dg is %d\n",dg);					      
						if (!dg) {
							clear_calling_tree(findme_user_list);
							return NULL;
						} else {
							tmpuser->state = -1;
						 	ast_hangup(winner);  
							livechannels--;
							if (option_debug)
								ast_log(LOG_DEBUG, "live channels left %d\n", livechannels);
							if (!livechannels) {
								if (option_verbose > 2)
									ast_verbose(VERBOSE_PREFIX_3 "no live channels left. exiting.\n");
								return NULL;
							}
						}
					}
				}					
				
			} else
				if (option_debug)
					ast_log(LOG_DEBUG, "timed out waiting for action\n");
		}
		
	} else {
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "couldn't reach at this number.\n");
	}
	
	/* --- WAIT FOR WINNER NUMBER END! -----------*/
	return NULL;
}

static void findmeexec(struct fm_args *tpargs)
{
	struct number *nm;
	struct ast_channel *outbound;
	struct ast_channel *caller;
	struct ast_channel *winner = NULL;
	char dialarg[512];
	int dg, idx;
	char *rest, *number;
	struct findme_user *tmpuser;
	struct findme_user *fmuser;
	struct findme_user *headuser;
	struct findme_user_listptr *findme_user_list;
	int status;

	findme_user_list = ast_calloc(1, sizeof(*findme_user_list));		
	AST_LIST_HEAD_INIT_NOLOCK(findme_user_list);

	/* We're going to figure out what the longest possible string of digits to collect is */
	ynlongest = 0;
	if (strlen(tpargs->takecall) > ynlongest)
		ynlongest = strlen(tpargs->takecall);
	if (strlen(tpargs->nextindp) > ynlongest)
		ynlongest = strlen(tpargs->nextindp);

	idx = 1;
	caller = tpargs->chan;
	AST_LIST_TRAVERSE(&tpargs->cnumbers, nm, entry)
		if (nm->order == idx)
			break;

	while (nm) {

		if (option_debug > 1)	
			ast_log(LOG_DEBUG, "Number %s timeout %ld\n", nm->number,nm->timeout);

		number = ast_strdupa(nm->number);
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "examining %s\n", number);
		do {
			rest = strchr(number, '&');
			if (rest) {
				*rest = 0;
				rest++;
			}

			if (!strcmp(tpargs->context, ""))
				snprintf(dialarg, sizeof(dialarg), "%s", number);
			else
				snprintf(dialarg, sizeof(dialarg), "%s@%s", number, tpargs->context);
					
			tmpuser = ast_calloc(1, sizeof(*tmpuser));
			if (!tmpuser) {
				ast_log(LOG_WARNING, "Out of memory!\n");
				free(findme_user_list);
				return;
			}
					
			outbound = ast_request("Local", ast_best_codec(caller->nativeformats), dialarg, &dg);
			if (outbound) {
				ast_set_callerid(outbound, caller->cid.cid_num, caller->cid.cid_name, caller->cid.cid_num);
				ast_channel_inherit_variables(tpargs->chan, outbound);
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "calling %s\n", dialarg);
				if (!ast_call(outbound,dialarg,0)) {
					tmpuser->ochan = outbound;
					tmpuser->state = 0;
					tmpuser->cleared = 0;
					ast_copy_string(tmpuser->dialarg, dialarg, sizeof(dialarg));
					AST_LIST_INSERT_TAIL(findme_user_list, tmpuser, entry);
				} else {
					if (option_verbose > 2) 
						ast_verbose(VERBOSE_PREFIX_3 "couldn't reach at this number.\n"); 
					if (outbound) {
						if (!outbound->cdr) 
							outbound->cdr = ast_cdr_alloc();
						if (outbound->cdr) {
							char tmp[256];

							ast_cdr_init(outbound->cdr, outbound);
							snprintf(tmp, sizeof(tmp), "%s/%s", "Local", dialarg);
							ast_cdr_setapp(outbound->cdr, "FollowMe", tmp);
							ast_cdr_update(outbound);
							ast_cdr_start(outbound->cdr);
							ast_cdr_end(outbound->cdr);
							/* If the cause wasn't handled properly */
							if (ast_cdr_disposition(outbound->cdr,outbound->hangupcause))
								ast_cdr_failed(outbound->cdr);
						} else {
							ast_log(LOG_ERROR, "Unable to create Call Detail Record\n");
							ast_hangup(outbound);
							outbound = NULL;
						}
					}
						
				}
			} else 
				ast_log(LOG_WARNING, "Unable to allocate a channel for Local/%s cause: %s\n", dialarg, ast_cause2str(dg));
					
			number = rest;
		} while (number);
				
		status = 0;	
		if (!AST_LIST_EMPTY(findme_user_list))
			winner = wait_for_winner(findme_user_list, nm, caller, tpargs->namerecloc, &status, tpargs);

		AST_LIST_TRAVERSE_SAFE_BEGIN(findme_user_list, fmuser, entry) {
			if (!fmuser->cleared && fmuser->ochan != winner)
				clear_caller(fmuser);
			AST_LIST_REMOVE_CURRENT(findme_user_list, entry);
			free(fmuser);
		}
		AST_LIST_TRAVERSE_SAFE_END;

		fmuser = NULL;
		tmpuser = NULL;
		headuser = NULL;	
		if (winner)
			break;

		if (!caller || ast_check_hangup(caller)) {
			tpargs->status = 1;
			free(findme_user_list);
			return;	
		}

		idx++;
		AST_LIST_TRAVERSE(&tpargs->cnumbers, nm, entry)
			if (nm->order == idx)
				break;

	}
	free(findme_user_list);
	if (!winner) 
		tpargs->status = 1;
	else {
		tpargs->status = 100;
		tpargs->outbound = winner;
	}

	
	return;
		
}

static void end_bridge_callback (void *data)
{
	char buf[80];
	time_t end;
	struct ast_channel *chan = data;

	time(&end);

	ast_channel_lock(chan);
	if (chan->cdr->answer.tv_sec) {
		snprintf(buf, sizeof(buf), "%ld", end - chan->cdr->answer.tv_sec);
		pbx_builtin_setvar_helper(chan, "ANSWEREDTIME", buf);
	}

	if (chan->cdr->start.tv_sec) {
		snprintf(buf, sizeof(buf), "%ld", end - chan->cdr->start.tv_sec);
		pbx_builtin_setvar_helper(chan, "DIALEDTIME", buf);
	}
	ast_channel_unlock(chan);
}

static void end_bridge_callback_data_fixup(struct ast_bridge_config *bconfig, struct ast_channel *originator, struct ast_channel *terminator)
{
	bconfig->end_bridge_callback_data = originator;
}

static int app_exec(struct ast_channel *chan, void *data)
{
	struct fm_args targs = { 0, };
	struct ast_bridge_config config;
	struct call_followme *f;
	struct number *nm, *newnm;
	int res = 0;
	struct ast_module_user *u;
	char *argstr;
	char namerecloc[255];
	char *fname = NULL;
	int duration = 0;
	struct ast_channel *caller;
	struct ast_channel *outbound;
	
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(followmeid);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument (followmeid)\n",app);
		return -1;
	}

	if (!(argstr = ast_strdupa((char *)data))) {
		ast_log(LOG_ERROR, "Out of memory!\n");
		return -1;
	}


	AST_STANDARD_APP_ARGS(args, argstr);
	if (ast_strlen_zero(args.followmeid)) {
		ast_log(LOG_WARNING, "%s requires an argument (followmeid)\n", app);
		return -1;
	}

	u = ast_module_user_add(chan);

	AST_LIST_LOCK(&followmes);
	AST_LIST_TRAVERSE(&followmes, f, entry) {
		if (!strcasecmp(f->name, args.followmeid) && (f->active))
			break;
	}
	AST_LIST_UNLOCK(&followmes);

	if (option_debug)
		ast_log(LOG_DEBUG, "New profile %s.\n", args.followmeid);
	if (!f) { 
		ast_log(LOG_WARNING, "Profile requested, %s, not found in the configuration.\n", args.followmeid);
		res = 0;
	} else {
		/* XXX TODO: Reinsert the db check value to see whether or not follow-me is on or off */


		if (args.options) 
			ast_app_parse_options(followme_opts, &targs.followmeflags, NULL, args.options);

		/* Lock the profile lock and copy out everything we need to run with before unlocking it again */
		ast_mutex_lock(&f->lock);
		targs.mohclass = ast_strdupa(f->moh);
		ast_copy_string(targs.context, f->context, sizeof(targs.context));
		ast_copy_string(targs.takecall, f->takecall, sizeof(targs.takecall));
		ast_copy_string(targs.nextindp, f->nextindp, sizeof(targs.nextindp));
		ast_copy_string(targs.callfromprompt, f->callfromprompt, sizeof(targs.callfromprompt));
		ast_copy_string(targs.norecordingprompt, f->norecordingprompt, sizeof(targs.norecordingprompt));
		ast_copy_string(targs.optionsprompt, f->optionsprompt, sizeof(targs.optionsprompt));
		ast_copy_string(targs.plsholdprompt, f->plsholdprompt, sizeof(targs.plsholdprompt));
		ast_copy_string(targs.statusprompt, f->statusprompt, sizeof(targs.statusprompt));
		ast_copy_string(targs.sorryprompt, f->sorryprompt, sizeof(targs.sorryprompt));
		/* Copy the numbers we're going to use into another list in case the master list should get modified 
				   (and locked) while we're trying to do a follow-me */
		AST_LIST_HEAD_INIT_NOLOCK(&targs.cnumbers);
		AST_LIST_TRAVERSE(&f->numbers, nm, entry) {
			newnm = create_followme_number(nm->number, "", nm->timeout, nm->order);
			AST_LIST_INSERT_TAIL(&targs.cnumbers, newnm, entry);
		}
		ast_mutex_unlock(&f->lock);

		if (ast_test_flag(&targs.followmeflags, FOLLOWMEFLAG_STATUSMSG)) 
			ast_stream_and_wait(chan, targs.statusprompt, chan->language, "");

		snprintf(namerecloc,sizeof(namerecloc),"%s/followme.%s",ast_config_AST_SPOOL_DIR,chan->uniqueid);
		duration = 5;

		if (ast_test_flag(&targs.followmeflags, FOLLOWMEFLAG_RECORDNAME)) 
			if (ast_play_and_record(chan, "vm-rec-name", namerecloc, 5, "sln", &duration, 128, 0, NULL) < 0)
				goto outrun;

		if (!ast_fileexists(namerecloc, NULL, chan->language))
			ast_copy_string(namerecloc, "", sizeof(namerecloc));					

		if (ast_streamfile(chan, targs.plsholdprompt, chan->language))
			goto outrun;
		if (ast_waitstream(chan, "") < 0)
			goto outrun;
		ast_moh_start(chan, S_OR(targs.mohclass, NULL), NULL);

		targs.status = 0;
		targs.chan = chan;
		ast_copy_string(targs.namerecloc, namerecloc, sizeof(targs.namerecloc));

		findmeexec(&targs);		
				
		AST_LIST_TRAVERSE_SAFE_BEGIN(&targs.cnumbers, nm, entry) {
			AST_LIST_REMOVE_CURRENT(&targs.cnumbers, entry);
			free(nm);
		}
		AST_LIST_TRAVERSE_SAFE_END
		if (targs.status != 100) {
			ast_moh_stop(chan);
			if (ast_test_flag(&targs.followmeflags, FOLLOWMEFLAG_UNREACHABLEMSG)) 
				ast_stream_and_wait(chan, targs.sorryprompt, chan->language, "");
			res = 0;
		} else {
			caller = chan;
			outbound = targs.outbound;
			/* Bridge the two channels. */

			memset(&config,0,sizeof(struct ast_bridge_config));
			ast_set_flag(&(config.features_callee), AST_FEATURE_REDIRECT);
			ast_set_flag(&(config.features_callee), AST_FEATURE_AUTOMON);
			ast_set_flag(&(config.features_caller), AST_FEATURE_AUTOMON);

			config.end_bridge_callback = end_bridge_callback;
			config.end_bridge_callback_data = chan;
			config.end_bridge_callback_data_fixup = end_bridge_callback_data_fixup;

			ast_moh_stop(caller);
			/* Be sure no generators are left on it */
			ast_deactivate_generator(caller);
			/* Make sure channels are compatible */
			res = ast_channel_make_compatible(caller, outbound);
			if (res < 0) {
				ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", caller->name, outbound->name);
				ast_hangup(outbound);
				goto outrun;
			}
			res = ast_bridge_call(caller,outbound,&config);
			if (outbound)
				ast_hangup(outbound);
		}
	}
	outrun:

	if (!ast_strlen_zero(namerecloc)){
		fname = alloca(strlen(namerecloc) + 5);
		sprintf(fname, "%s.sln", namerecloc);
		unlink(fname);
	}
	
	ast_module_user_remove(u);

	return res;
}

static int unload_module(void)
{
	struct call_followme *f;

	ast_module_user_hangup_all();

	ast_unregister_application(app);

	/* Free Memory. Yeah! I'm free! */
	AST_LIST_LOCK(&followmes);
	while ((f = AST_LIST_REMOVE_HEAD(&followmes, entry))) {
		free_numbers(f);
		free(f);
	}

	AST_LIST_UNLOCK(&followmes);

	return 0;
}

static int load_module(void)
{
	if(!reload_followme())
		return AST_MODULE_LOAD_DECLINE;

	return ast_register_application(app, app_exec, synopsis, descrip);
}

static int reload(void)
{
	reload_followme();

	return 0;	
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Find-Me/Follow-Me Application",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
