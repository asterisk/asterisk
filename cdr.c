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
 * \brief Call Detail Record API 
 * 
 * Includes code and algorithms from the Zapata library.
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/logger.h"
#include "asterisk/callerid.h"
#include "asterisk/causes.h"
#include "asterisk/options.h"
#include "asterisk/linkedlists.h"
#include "asterisk/utils.h"
#include "asterisk/sched.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/module.h"

int ast_default_amaflags = AST_CDR_DOCUMENTATION;
char ast_default_accountcode[AST_MAX_ACCOUNT_CODE] = "";

struct ast_cdr_beitem {
	char name[20];
	char desc[80];
	ast_cdrbe be;
	AST_LIST_ENTRY(ast_cdr_beitem) list;
};

static AST_LIST_HEAD_STATIC(be_list, ast_cdr_beitem);

struct ast_cdr_batch_item {
	struct ast_cdr *cdr;
	struct ast_cdr_batch_item *next;
};

static struct ast_cdr_batch {
	int size;
	struct ast_cdr_batch_item *head;
	struct ast_cdr_batch_item *tail;
} *batch = NULL;

static struct sched_context *sched;
static int cdr_sched = -1;
static pthread_t cdr_thread = AST_PTHREADT_NULL;

#define BATCH_SIZE_DEFAULT 100
#define BATCH_TIME_DEFAULT 300
#define BATCH_SCHEDULER_ONLY_DEFAULT 0
#define BATCH_SAFE_SHUTDOWN_DEFAULT 1

static int enabled;
static int batchmode;
static int batchsize;
static int batchtime;
static int batchscheduleronly;
static int batchsafeshutdown;

AST_MUTEX_DEFINE_STATIC(cdr_batch_lock);

/* these are used to wake up the CDR thread when there's work to do */
AST_MUTEX_DEFINE_STATIC(cdr_pending_lock);
static pthread_cond_t cdr_pending_cond;

/*
 * We do a lot of checking here in the CDR code to try to be sure we don't ever let a CDR slip
 * through our fingers somehow.  If someone allocates a CDR, it must be completely handled normally
 * or a WARNING shall be logged, so that we can best keep track of any escape condition where the CDR
 * isn't properly generated and posted.
 */

int ast_cdr_register(char *name, char *desc, ast_cdrbe be)
{
	struct ast_cdr_beitem *i;

	if (!name)
		return -1;
	if (!be) {
		ast_log(LOG_WARNING, "CDR engine '%s' lacks backend\n", name);
		return -1;
	}

	AST_LIST_LOCK(&be_list);
	AST_LIST_TRAVERSE(&be_list, i, list) {
		if (!strcasecmp(name, i->name))
			break;
	}
	AST_LIST_UNLOCK(&be_list);

	if (i) {
		ast_log(LOG_WARNING, "Already have a CDR backend called '%s'\n", name);
		return -1;
	}

	i = malloc(sizeof(*i));
	if (!i) 	
		return -1;

	memset(i, 0, sizeof(*i));
	i->be = be;
	ast_copy_string(i->name, name, sizeof(i->name));
	ast_copy_string(i->desc, desc, sizeof(i->desc));

	AST_LIST_LOCK(&be_list);
	AST_LIST_INSERT_HEAD(&be_list, i, list);
	AST_LIST_UNLOCK(&be_list);

	return 0;
}

void ast_cdr_unregister(char *name)
{
	struct ast_cdr_beitem *i = NULL;

	AST_LIST_LOCK(&be_list);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&be_list, i, list) {
		if (!strcasecmp(name, i->name)) {
			AST_LIST_REMOVE_CURRENT(&be_list, list);
			if (option_verbose > 1)
				ast_verbose(VERBOSE_PREFIX_2 "Unregistered '%s' CDR backend\n", name);
			free(i);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&be_list);
}

struct ast_cdr *ast_cdr_dup(struct ast_cdr *cdr) 
{
	struct ast_cdr *newcdr;

	if (!(newcdr = ast_cdr_alloc())) {
		ast_log(LOG_ERROR, "Memory Error!\n");
		return NULL;
	}

	memcpy(newcdr, cdr, sizeof(*newcdr));
	/* The varshead is unusable, volatile even, after the memcpy so we take care of that here */
	memset(&newcdr->varshead, 0, sizeof(newcdr->varshead));
	ast_cdr_copy_vars(newcdr, cdr);
	newcdr->next = NULL;

	return newcdr;
}

static const char *ast_cdr_getvar_internal(struct ast_cdr *cdr, const char *name, int recur) 
{
	struct ast_var_t *variables;
	struct varshead *headp;

	if (ast_strlen_zero(name))
		return NULL;

	while (cdr) {
		headp = &cdr->varshead;
		AST_LIST_TRAVERSE(headp, variables, entries) {
			if (!strcasecmp(name, ast_var_name(variables)))
				return ast_var_value(variables);
		}
		if (!recur)
			break;
		cdr = cdr->next;
	}

	return NULL;
}

void ast_cdr_getvar(struct ast_cdr *cdr, const char *name, char **ret, char *workspace, int workspacelen, int recur) 
{
	struct tm tm;
	time_t t;
	const char *fmt = "%Y-%m-%d %T";
	const char *varbuf;

	*ret = NULL;
	/* special vars (the ones from the struct ast_cdr when requested by name) 
	   I'd almost say we should convert all the stringed vals to vars */

	if (!strcasecmp(name, "clid"))
		ast_copy_string(workspace, cdr->clid, workspacelen);
	else if (!strcasecmp(name, "src"))
		ast_copy_string(workspace, cdr->src, workspacelen);
	else if (!strcasecmp(name, "dst"))
		ast_copy_string(workspace, cdr->dst, workspacelen);
	else if (!strcasecmp(name, "dcontext"))
		ast_copy_string(workspace, cdr->dcontext, workspacelen);
	else if (!strcasecmp(name, "channel"))
		ast_copy_string(workspace, cdr->channel, workspacelen);
	else if (!strcasecmp(name, "dstchannel"))
		ast_copy_string(workspace, cdr->dstchannel, workspacelen);
	else if (!strcasecmp(name, "lastapp"))
		ast_copy_string(workspace, cdr->lastapp, workspacelen);
	else if (!strcasecmp(name, "lastdata"))
		ast_copy_string(workspace, cdr->lastdata, workspacelen);
	else if (!strcasecmp(name, "start")) {
		t = cdr->start.tv_sec;
		if (t) {
			localtime_r(&t, &tm);
			strftime(workspace, workspacelen, fmt, &tm);
		}
	} else if (!strcasecmp(name, "answer")) {
		t = cdr->answer.tv_sec;
		if (t) {
			localtime_r(&t, &tm);
			strftime(workspace, workspacelen, fmt, &tm);
		}
	} else if (!strcasecmp(name, "end")) {
		t = cdr->end.tv_sec;
		if (t) {
			localtime_r(&t, &tm);
			strftime(workspace, workspacelen, fmt, &tm);
		}
	} else if (!strcasecmp(name, "duration"))
		snprintf(workspace, workspacelen, "%d", cdr->duration);
	else if (!strcasecmp(name, "billsec"))
		snprintf(workspace, workspacelen, "%d", cdr->billsec);
	else if (!strcasecmp(name, "disposition"))
		ast_copy_string(workspace, ast_cdr_disp2str(cdr->disposition), workspacelen);
	else if (!strcasecmp(name, "amaflags"))
		ast_copy_string(workspace, ast_cdr_flags2str(cdr->amaflags), workspacelen);
	else if (!strcasecmp(name, "accountcode"))
		ast_copy_string(workspace, cdr->accountcode, workspacelen);
	else if (!strcasecmp(name, "uniqueid"))
		ast_copy_string(workspace, cdr->uniqueid, workspacelen);
	else if (!strcasecmp(name, "userfield"))
		ast_copy_string(workspace, cdr->userfield, workspacelen);
	else if ((varbuf = ast_cdr_getvar_internal(cdr, name, recur)))
		ast_copy_string(workspace, varbuf, workspacelen);

	if (!ast_strlen_zero(workspace))
		*ret = workspace;
}

int ast_cdr_setvar(struct ast_cdr *cdr, const char *name, const char *value, int recur) 
{
	struct ast_var_t *newvariable;
	struct varshead *headp;
	const char *read_only[] = { "clid", "src", "dst", "dcontext", "channel", "dstchannel",
				    "lastapp", "lastdata", "start", "answer", "end", "duration",
				    "billsec", "disposition", "amaflags", "accountcode", "uniqueid",
				    "userfield", NULL };
	int x;
	
	for(x = 0; read_only[x]; x++) {
		if (!strcasecmp(name, read_only[x])) {
			ast_log(LOG_ERROR, "Attempt to set a read-only variable!.\n");
			return -1;
		}
	}

	if (!cdr) {
		ast_log(LOG_ERROR, "Attempt to set a variable on a nonexistent CDR record.\n");
		return -1;
	}

	while (cdr) {
		headp = &cdr->varshead;
		AST_LIST_TRAVERSE_SAFE_BEGIN(headp, newvariable, entries) {
			if (!strcasecmp(ast_var_name(newvariable), name)) {
				/* there is already such a variable, delete it */
				AST_LIST_REMOVE_CURRENT(headp, entries);
				ast_var_delete(newvariable);
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;

		if (value) {
			newvariable = ast_var_assign(name, value);
			AST_LIST_INSERT_HEAD(headp, newvariable, entries);
		}

		if (!recur) {
			break;
		}

		cdr = cdr->next;
	}

	return 0;
}

int ast_cdr_copy_vars(struct ast_cdr *to_cdr, struct ast_cdr *from_cdr)
{
	struct ast_var_t *variables, *newvariable = NULL;
	struct varshead *headpa, *headpb;
	char *var, *val;
	int x = 0;

	headpa = &from_cdr->varshead;
	headpb = &to_cdr->varshead;

	AST_LIST_TRAVERSE(headpa,variables,entries) {
		if (variables &&
		    (var = ast_var_name(variables)) && (val = ast_var_value(variables)) &&
		    !ast_strlen_zero(var) && !ast_strlen_zero(val)) {
			newvariable = ast_var_assign(var, val);
			AST_LIST_INSERT_HEAD(headpb, newvariable, entries);
			x++;
		}
	}

	return x;
}

int ast_cdr_serialize_variables(struct ast_cdr *cdr, char *buf, size_t size, char delim, char sep, int recur) 
{
	struct ast_var_t *variables;
	char *var, *val;
	char *tmp;
	char workspace[256];
	int total = 0, x = 0, i;
	const char *cdrcols[] = { 
		"clid",
		"src",
		"dst",
		"dcontext",
		"channel",
		"dstchannel",
		"lastapp",
		"lastdata",
		"start",
		"answer",
		"end",
		"duration",
		"billsec",
		"disposition",
		"amaflags",
		"accountcode",
		"uniqueid",
		"userfield"
	};

	memset(buf, 0, size);

	for (; cdr; cdr = recur ? cdr->next : NULL) {
		if (++x > 1)
			ast_build_string(&buf, &size, "\n");

		AST_LIST_TRAVERSE(&cdr->varshead, variables, entries) {
			if (variables &&
			    (var = ast_var_name(variables)) && (val = ast_var_value(variables)) &&
			    !ast_strlen_zero(var) && !ast_strlen_zero(val)) {
				if (ast_build_string(&buf, &size, "level %d: %s%c%s%c", x, var, delim, val, sep)) {
 					ast_log(LOG_ERROR, "Data Buffer Size Exceeded!\n");
 					break;
				} else
					total++;
			} else 
				break;
		}

		for (i = 0; i < (sizeof(cdrcols) / sizeof(cdrcols[0])); i++) {
			ast_cdr_getvar(cdr, cdrcols[i], &tmp, workspace, sizeof(workspace), 0);
			if (!tmp)
				continue;
			
			if (ast_build_string(&buf, &size, "level %d: %s%c%s%c", x, cdrcols[i], delim, tmp, sep)) {
				ast_log(LOG_ERROR, "Data Buffer Size Exceeded!\n");
				break;
			} else
				total++;
		}
	}

	return total;
}


void ast_cdr_free_vars(struct ast_cdr *cdr, int recur)
{
	struct varshead *headp;
	struct ast_var_t *vardata;

	/* clear variables */
	while (cdr) {
		headp = &cdr->varshead;
		while (!AST_LIST_EMPTY(headp)) {
			vardata = AST_LIST_REMOVE_HEAD(headp, entries);
			ast_var_delete(vardata);
		}

		if (!recur) {
			break;
		}

		cdr = cdr->next;
	}
}

void ast_cdr_free(struct ast_cdr *cdr)
{
	char *chan;
	struct ast_cdr *next; 

	while (cdr) {
		next = cdr->next;
		chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (!ast_test_flag(cdr, AST_CDR_FLAG_POSTED) && !ast_test_flag(cdr, AST_CDR_FLAG_POST_DISABLED))
			ast_log(LOG_WARNING, "CDR on channel '%s' not posted\n", chan);
		if (ast_tvzero(cdr->end))
			ast_log(LOG_WARNING, "CDR on channel '%s' lacks end\n", chan);
		if (ast_tvzero(cdr->start))
			ast_log(LOG_WARNING, "CDR on channel '%s' lacks start\n", chan);

		ast_cdr_free_vars(cdr, 0);
		free(cdr);
		cdr = next;
	}
}

struct ast_cdr *ast_cdr_alloc(void)
{
	struct ast_cdr *cdr;

	cdr = malloc(sizeof(*cdr));
	if (cdr)
		memset(cdr, 0, sizeof(*cdr));

	return cdr;
}

void ast_cdr_start(struct ast_cdr *cdr)
{
	char *chan; 

	while (cdr) {
		if (!ast_test_flag(cdr, AST_CDR_FLAG_LOCKED)) {
			chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (ast_test_flag(cdr, AST_CDR_FLAG_POSTED))
				ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
			if (!ast_tvzero(cdr->start))
				ast_log(LOG_WARNING, "CDR on channel '%s' already started\n", chan);
			cdr->start = ast_tvnow();
		}
		cdr = cdr->next;
	}
}

void ast_cdr_answer(struct ast_cdr *cdr)
{
	char *chan; 

	while (cdr) {
		chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (ast_test_flag(cdr, AST_CDR_FLAG_POSTED))
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (cdr->disposition < AST_CDR_ANSWERED)
			cdr->disposition = AST_CDR_ANSWERED;
		if (ast_tvzero(cdr->answer))
			cdr->answer = ast_tvnow();
		cdr = cdr->next;
	}
}

void ast_cdr_busy(struct ast_cdr *cdr)
{
	char *chan; 

	while (cdr) {
		if (!ast_test_flag(cdr, AST_CDR_FLAG_LOCKED)) {
			chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (ast_test_flag(cdr, AST_CDR_FLAG_POSTED))
				ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
			if (cdr->disposition < AST_CDR_BUSY)
				cdr->disposition = AST_CDR_BUSY;
		}
		cdr = cdr->next;
	}
}

void ast_cdr_failed(struct ast_cdr *cdr)
{
	char *chan; 

	while (cdr) {
		chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (ast_test_flag(cdr, AST_CDR_FLAG_POSTED))
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (!ast_test_flag(cdr, AST_CDR_FLAG_LOCKED))
			cdr->disposition = AST_CDR_FAILED;
		cdr = cdr->next;
	}
}

int ast_cdr_disposition(struct ast_cdr *cdr, int cause)
{
	int res = 0;

	while (cdr) {
		switch(cause) {
		case AST_CAUSE_BUSY:
			ast_cdr_busy(cdr);
			break;
		case AST_CAUSE_FAILURE:
			ast_cdr_failed(cdr);
			break;
		case AST_CAUSE_NORMAL:
			break;
		case AST_CAUSE_NOTDEFINED:
			res = -1;
			break;
		default:
			res = -1;
			ast_log(LOG_WARNING, "Cause not handled\n");
		}
		cdr = cdr->next;
	}
	return res;
}

void ast_cdr_setdestchan(struct ast_cdr *cdr, char *chann)
{
	char *chan; 

	while (cdr) {
		chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (ast_test_flag(cdr, AST_CDR_FLAG_POSTED))
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (!ast_test_flag(cdr, AST_CDR_FLAG_LOCKED))
			ast_copy_string(cdr->dstchannel, chann, sizeof(cdr->dstchannel));
		cdr = cdr->next;
	}
}

void ast_cdr_setapp(struct ast_cdr *cdr, char *app, char *data)
{
	char *chan; 

	while (cdr) {
		if (!ast_test_flag(cdr, AST_CDR_FLAG_LOCKED)) {
			chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (ast_test_flag(cdr, AST_CDR_FLAG_POSTED))
				ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
			if (!app)
				app = "";
			ast_copy_string(cdr->lastapp, app, sizeof(cdr->lastapp));
			if (!data)
				data = "";
			ast_copy_string(cdr->lastdata, data, sizeof(cdr->lastdata));
		}
		cdr = cdr->next;
	}
}

int ast_cdr_setcid(struct ast_cdr *cdr, struct ast_channel *c)
{
	char tmp[AST_MAX_EXTENSION] = "";
	char *num;

	while (cdr) {
		if (!ast_test_flag(cdr, AST_CDR_FLAG_LOCKED)) {
			/* Grab source from ANI or normal Caller*ID */
			num = c->cid.cid_ani ? c->cid.cid_ani : c->cid.cid_num;
			
			if (c->cid.cid_name && num)
				snprintf(tmp, sizeof(tmp), "\"%s\" <%s>", c->cid.cid_name, num);
			else if (c->cid.cid_name)
				ast_copy_string(tmp, c->cid.cid_name, sizeof(tmp));
			else if (num)
				ast_copy_string(tmp, num, sizeof(tmp));
			ast_copy_string(cdr->clid, tmp, sizeof(cdr->clid));
			ast_copy_string(cdr->src, num ? num : "", sizeof(cdr->src));
		}
		cdr = cdr->next;
	}

	return 0;
}


int ast_cdr_init(struct ast_cdr *cdr, struct ast_channel *c)
{
	char *chan;
	char *num;
	char tmp[AST_MAX_EXTENSION] = "";

	while (cdr) {
		if (!ast_test_flag(cdr, AST_CDR_FLAG_LOCKED)) {
			chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (!ast_strlen_zero(cdr->channel)) 
				ast_log(LOG_WARNING, "CDR already initialized on '%s'\n", chan); 
			ast_copy_string(cdr->channel, c->name, sizeof(cdr->channel));
			/* Grab source from ANI or normal Caller*ID */
			num = c->cid.cid_ani ? c->cid.cid_ani : c->cid.cid_num;
			
			if (c->cid.cid_name && num)
				snprintf(tmp, sizeof(tmp), "\"%s\" <%s>", c->cid.cid_name, num);
			else if (c->cid.cid_name)
				ast_copy_string(tmp, c->cid.cid_name, sizeof(tmp));
			else if (num)
				ast_copy_string(tmp, num, sizeof(tmp));
			ast_copy_string(cdr->clid, tmp, sizeof(cdr->clid));
			ast_copy_string(cdr->src, num ? num : "", sizeof(cdr->src));

			cdr->disposition = (c->_state == AST_STATE_UP) ?  AST_CDR_ANSWERED : AST_CDR_NOANSWER;
			cdr->amaflags = c->amaflags ? c->amaflags :  ast_default_amaflags;
			ast_copy_string(cdr->accountcode, c->accountcode, sizeof(cdr->accountcode));
			/* Destination information */
			ast_copy_string(cdr->dst, c->exten, sizeof(cdr->dst));
			ast_copy_string(cdr->dcontext, c->context, sizeof(cdr->dcontext));
			/* Unique call identifier */
			ast_copy_string(cdr->uniqueid, c->uniqueid, sizeof(cdr->uniqueid));
		}
		cdr = cdr->next;
	}
	return 0;
}

void ast_cdr_end(struct ast_cdr *cdr)
{
	char *chan;

	while (cdr) {
		chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (ast_test_flag(cdr, AST_CDR_FLAG_POSTED))
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (ast_tvzero(cdr->start))
			ast_log(LOG_WARNING, "CDR on channel '%s' has not started\n", chan);
		if (ast_tvzero(cdr->end))
			cdr->end = ast_tvnow();
		cdr = cdr->next;
	}
}

char *ast_cdr_disp2str(int disposition)
{
	switch (disposition) {
	case AST_CDR_NOANSWER:
		return "NO ANSWER";
	case AST_CDR_FAILED:
		return "FAILED";		
	case AST_CDR_BUSY:
		return "BUSY";		
	case AST_CDR_ANSWERED:
		return "ANSWERED";
	}
	return "UNKNOWN";
}

char *ast_cdr_flags2str(int flag)
{
	switch(flag) {
	case AST_CDR_OMIT:
		return "OMIT";
	case AST_CDR_BILLING:
		return "BILLING";
	case AST_CDR_DOCUMENTATION:
		return "DOCUMENTATION";
	}
	return "Unknown";
}

int ast_cdr_setaccount(struct ast_channel *chan, const char *account)
{
	struct ast_cdr *cdr = chan->cdr;

	ast_copy_string(chan->accountcode, account, sizeof(chan->accountcode));
	while (cdr) {
		if (!ast_test_flag(cdr, AST_CDR_FLAG_LOCKED))
			ast_copy_string(cdr->accountcode, chan->accountcode, sizeof(cdr->accountcode));
		cdr = cdr->next;
	}
	return 0;
}

int ast_cdr_setamaflags(struct ast_channel *chan, const char *flag)
{
	struct ast_cdr *cdr = chan->cdr;
	int newflag;

	newflag = ast_cdr_amaflags2int(flag);
	if (newflag)
		cdr->amaflags = newflag;

	return 0;
}

int ast_cdr_setuserfield(struct ast_channel *chan, const char *userfield)
{
	struct ast_cdr *cdr = chan->cdr;

	while (cdr) {
		if (!ast_test_flag(cdr, AST_CDR_FLAG_LOCKED)) 
			ast_copy_string(cdr->userfield, userfield, sizeof(cdr->userfield));
		cdr = cdr->next;
	}

	return 0;
}

int ast_cdr_appenduserfield(struct ast_channel *chan, const char *userfield)
{
	struct ast_cdr *cdr = chan->cdr;

	while (cdr) {
		int len = strlen(cdr->userfield);

		if (!ast_test_flag(cdr, AST_CDR_FLAG_LOCKED))
			strncpy(cdr->userfield+len, userfield, sizeof(cdr->userfield) - len - 1);

		cdr = cdr->next;
	}

	return 0;
}

int ast_cdr_update(struct ast_channel *c)
{
	struct ast_cdr *cdr = c->cdr;
	char *num;
	char tmp[AST_MAX_EXTENSION] = "";

	while (cdr) {
		if (!ast_test_flag(cdr, AST_CDR_FLAG_LOCKED)) {
			num = c->cid.cid_ani ? c->cid.cid_ani : c->cid.cid_num;
			
			if (c->cid.cid_name && num)
				snprintf(tmp, sizeof(tmp), "\"%s\" <%s>", c->cid.cid_name, num);
			else if (c->cid.cid_name)
				ast_copy_string(tmp, c->cid.cid_name, sizeof(tmp));
			else if (num)
				ast_copy_string(tmp, num, sizeof(tmp));
			ast_copy_string(cdr->clid, tmp, sizeof(cdr->clid));
			ast_copy_string(cdr->src, num ? num : "", sizeof(cdr->src));

			/* Copy account code et-al */	
			ast_copy_string(cdr->accountcode, c->accountcode, sizeof(cdr->accountcode));
			/* Destination information */
			ast_copy_string(cdr->dst, (ast_strlen_zero(c->macroexten)) ? c->exten : c->macroexten, sizeof(cdr->dst));
			ast_copy_string(cdr->dcontext, (ast_strlen_zero(c->macrocontext)) ? c->context : c->macrocontext, sizeof(cdr->dcontext));
		}
		cdr = cdr->next;
	}

	return 0;
}

int ast_cdr_amaflags2int(const char *flag)
{
	if (!strcasecmp(flag, "default"))
		return 0;
	if (!strcasecmp(flag, "omit"))
		return AST_CDR_OMIT;
	if (!strcasecmp(flag, "billing"))
		return AST_CDR_BILLING;
	if (!strcasecmp(flag, "documentation"))
		return AST_CDR_DOCUMENTATION;
	return -1;
}

static void post_cdr(struct ast_cdr *cdr)
{
	char *chan;
	struct ast_cdr_beitem *i;

	while (cdr) {
		chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (ast_test_flag(cdr, AST_CDR_FLAG_POSTED))
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (ast_tvzero(cdr->end))
			ast_log(LOG_WARNING, "CDR on channel '%s' lacks end\n", chan);
		if (ast_tvzero(cdr->start))
			ast_log(LOG_WARNING, "CDR on channel '%s' lacks start\n", chan);
		cdr->duration = cdr->end.tv_sec - cdr->start.tv_sec + (cdr->end.tv_usec - cdr->start.tv_usec) / 1000000;
		if (!ast_tvzero(cdr->answer))
			cdr->billsec = cdr->end.tv_sec - cdr->answer.tv_sec + (cdr->end.tv_usec - cdr->answer.tv_usec) / 1000000;
		else
			cdr->billsec = 0;
		ast_set_flag(cdr, AST_CDR_FLAG_POSTED);
		AST_LIST_LOCK(&be_list);
		AST_LIST_TRAVERSE(&be_list, i, list) {
			i->be(cdr);
		}
		AST_LIST_UNLOCK(&be_list);
		cdr = cdr->next;
	}
}

void ast_cdr_reset(struct ast_cdr *cdr, int flags)
{
	struct ast_flags tmp = {flags};
	struct ast_cdr *dup;


	while (cdr) {
		/* Detach if post is requested */
		if (ast_test_flag(&tmp, AST_CDR_FLAG_LOCKED) || !ast_test_flag(cdr, AST_CDR_FLAG_LOCKED)) {
			if (ast_test_flag(&tmp, AST_CDR_FLAG_POSTED)) {
				ast_cdr_end(cdr);
				if ((dup = ast_cdr_dup(cdr))) {
					ast_cdr_detach(dup);
				}
				ast_set_flag(cdr, AST_CDR_FLAG_POSTED);
			}

			/* clear variables */
			if (!ast_test_flag(&tmp, AST_CDR_FLAG_KEEP_VARS)) {
				ast_cdr_free_vars(cdr, 0);
			}

			/* Reset to initial state */
			ast_clear_flag(cdr, AST_FLAGS_ALL);	
			memset(&cdr->start, 0, sizeof(cdr->start));
			memset(&cdr->end, 0, sizeof(cdr->end));
			memset(&cdr->answer, 0, sizeof(cdr->answer));
			cdr->billsec = 0;
			cdr->duration = 0;
			ast_cdr_start(cdr);
			cdr->disposition = AST_CDR_NOANSWER;
		}
			
		cdr = cdr->next;
	}
}

struct ast_cdr *ast_cdr_append(struct ast_cdr *cdr, struct ast_cdr *newcdr) 
{
	struct ast_cdr *ret;

	if (cdr) {
		ret = cdr;

		while (cdr->next)
			cdr = cdr->next;
		cdr->next = newcdr;
	} else {
		ret = newcdr;
	}

	return ret;
}

/* Don't call without cdr_batch_lock */
static void reset_batch(void)
{
	batch->size = 0;
	batch->head = NULL;
	batch->tail = NULL;
}

/* Don't call without cdr_batch_lock */
static int init_batch(void)
{
	/* This is the single meta-batch used to keep track of all CDRs during the entire life of the program */
	batch = malloc(sizeof(*batch));
	if (!batch) {
		ast_log(LOG_WARNING, "CDR: out of memory while trying to handle batched records, data will most likely be lost\n");
		return -1;
	}

	reset_batch();

	return 0;
}

static void *do_batch_backend_process(void *data)
{
	struct ast_cdr_batch_item *processeditem;
	struct ast_cdr_batch_item *batchitem = data;

	/* Push each CDR into storage mechanism(s) and free all the memory */
	while (batchitem) {
		post_cdr(batchitem->cdr);
		ast_cdr_free(batchitem->cdr);
		processeditem = batchitem;
		batchitem = batchitem->next;
		free(processeditem);
	}

	return NULL;
}

void ast_cdr_submit_batch(int shutdown)
{
	struct ast_cdr_batch_item *oldbatchitems = NULL;
	pthread_attr_t attr;
	pthread_t batch_post_thread = AST_PTHREADT_NULL;

	/* if there's no batch, or no CDRs in the batch, then there's nothing to do */
	if (!batch || !batch->head)
		return;

	/* move the old CDRs aside, and prepare a new CDR batch */
	ast_mutex_lock(&cdr_batch_lock);
	oldbatchitems = batch->head;
	reset_batch();
	ast_mutex_unlock(&cdr_batch_lock);

	/* if configured, spawn a new thread to post these CDRs,
	   also try to save as much as possible if we are shutting down safely */
	if (batchscheduleronly || shutdown) {
		if (option_debug)
			ast_log(LOG_DEBUG, "CDR single-threaded batch processing begins now\n");
		do_batch_backend_process(oldbatchitems);
	} else {
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (ast_pthread_create(&batch_post_thread, &attr, do_batch_backend_process, oldbatchitems)) {
			ast_log(LOG_WARNING, "CDR processing thread could not detach, now trying in this thread\n");
			do_batch_backend_process(oldbatchitems);
		} else {
			if (option_debug)
				ast_log(LOG_DEBUG, "CDR multi-threaded batch processing begins now\n");
		}
	}
}

static int submit_scheduled_batch(void *data)
{
	ast_cdr_submit_batch(0);
	/* manually reschedule from this point in time */
	cdr_sched = ast_sched_add(sched, batchtime * 1000, submit_scheduled_batch, NULL);
	/* returning zero so the scheduler does not automatically reschedule */
	return 0;
}

static void submit_unscheduled_batch(void)
{
	/* this is okay since we are not being called from within the scheduler */
	if (cdr_sched > -1)
		ast_sched_del(sched, cdr_sched);
	/* schedule the submission to occur ASAP (1 ms) */
	cdr_sched = ast_sched_add(sched, 1, submit_scheduled_batch, NULL);
	/* signal the do_cdr thread to wakeup early and do some work (that lazy thread ;) */
	ast_mutex_lock(&cdr_pending_lock);
	pthread_cond_signal(&cdr_pending_cond);
	ast_mutex_unlock(&cdr_pending_lock);
}

void ast_cdr_detach(struct ast_cdr *cdr)
{
	struct ast_cdr_batch_item *newtail;
	int curr;

	/* maybe they disabled CDR stuff completely, so just drop it */
	if (!enabled) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Dropping CDR !\n");
		ast_set_flag(cdr, AST_CDR_FLAG_POST_DISABLED);
		ast_cdr_free(cdr);
		return;
	}

	/* post stuff immediately if we are not in batch mode, this is legacy behaviour */
	if (!batchmode) {
		post_cdr(cdr);
		ast_cdr_free(cdr);
		return;
	}

	/* otherwise, each CDR gets put into a batch list (at the end) */
	if (option_debug)
		ast_log(LOG_DEBUG, "CDR detaching from this thread\n");

	/* we'll need a new tail for every CDR */
	newtail = malloc(sizeof(*newtail));
	if (!newtail) {
		ast_log(LOG_WARNING, "CDR: out of memory while trying to detach, will try in this thread instead\n");
		post_cdr(cdr);
		ast_cdr_free(cdr);
		return;
	}
	memset(newtail, 0, sizeof(*newtail));

	/* don't traverse a whole list (just keep track of the tail) */
	ast_mutex_lock(&cdr_batch_lock);
	if (!batch)
		init_batch();
	if (!batch->head) {
		/* new batch is empty, so point the head at the new tail */
		batch->head = newtail;
	} else {
		/* already got a batch with something in it, so just append a new tail */
		batch->tail->next = newtail;
	}
	newtail->cdr = cdr;
	batch->tail = newtail;
	curr = batch->size++;
	ast_mutex_unlock(&cdr_batch_lock);

	/* if we have enough stuff to post, then do it */
	if (curr >= (batchsize - 1))
		submit_unscheduled_batch();
}

static void *do_cdr(void *data)
{
	struct timespec timeout;
	int schedms;
	int numevents = 0;

	for(;;) {
		struct timeval now = ast_tvnow();
		schedms = ast_sched_wait(sched);
		/* this shouldn't happen, but provide a 1 second default just in case */
		if (schedms <= 0)
			schedms = 1000;
		timeout.tv_sec = now.tv_sec + (schedms / 1000);
		timeout.tv_nsec = (now.tv_usec * 1000) + ((schedms % 1000) * 1000);
		/* prevent stuff from clobbering cdr_pending_cond, then wait on signals sent to it until the timeout expires */
		ast_mutex_lock(&cdr_pending_lock);
		ast_pthread_cond_timedwait(&cdr_pending_cond, &cdr_pending_lock, &timeout);
		numevents = ast_sched_runq(sched);
		ast_mutex_unlock(&cdr_pending_lock);
		if (option_debug > 1)
			ast_log(LOG_DEBUG, "Processed %d scheduled CDR batches from the run queue\n", numevents);
	}

	return NULL;
}

static int handle_cli_status(int fd, int argc, char *argv[])
{
	struct ast_cdr_beitem *beitem=NULL;
	int cnt=0;
	long nextbatchtime=0;

	if (argc > 2)
		return RESULT_SHOWUSAGE;

	ast_cli(fd, "CDR logging: %s\n", enabled ? "enabled" : "disabled");
	ast_cli(fd, "CDR mode: %s\n", batchmode ? "batch" : "simple");
	if (enabled) {
		if (batchmode) {
			if (batch)
				cnt = batch->size;
			if (cdr_sched > -1)
				nextbatchtime = ast_sched_when(sched, cdr_sched);
			ast_cli(fd, "CDR safe shut down: %s\n", batchsafeshutdown ? "enabled" : "disabled");
			ast_cli(fd, "CDR batch threading model: %s\n", batchscheduleronly ? "scheduler only" : "scheduler plus separate threads");
			ast_cli(fd, "CDR current batch size: %d record%s\n", cnt, (cnt != 1) ? "s" : "");
			ast_cli(fd, "CDR maximum batch size: %d record%s\n", batchsize, (batchsize != 1) ? "s" : "");
			ast_cli(fd, "CDR maximum batch time: %d second%s\n", batchtime, (batchtime != 1) ? "s" : "");
			ast_cli(fd, "CDR next scheduled batch processing time: %ld second%s\n", nextbatchtime, (nextbatchtime != 1) ? "s" : "");
		}
		AST_LIST_LOCK(&be_list);
		AST_LIST_TRAVERSE(&be_list, beitem, list) {
			ast_cli(fd, "CDR registered backend: %s\n", beitem->name);
		}
		AST_LIST_UNLOCK(&be_list);
	}

	return 0;
}

static int handle_cli_submit(int fd, int argc, char *argv[])
{
	if (argc > 2)
		return RESULT_SHOWUSAGE;

	submit_unscheduled_batch();
	ast_cli(fd, "Submitted CDRs to backend engines for processing.  This may take a while.\n");

	return 0;
}

static struct ast_cli_entry cli_submit = {
	.cmda = { "cdr", "submit", NULL },
	.handler = handle_cli_submit,
	.summary = "Posts all pending batched CDR data",
	.usage =
	"Usage: cdr submit\n"
	"       Posts all pending batched CDR data to the configured CDR backend engine modules.\n"
};

static struct ast_cli_entry cli_status = {
	.cmda = { "cdr", "status", NULL },
	.handler = handle_cli_status,
	.summary = "Display the CDR status",
	.usage =
	"Usage: cdr status\n"
	"	Displays the Call Detail Record engine system status.\n"
};

static int do_reload(void)
{
	struct ast_config *config;
	const char *enabled_value;
	const char *batched_value;
	const char *scheduleronly_value;
	const char *batchsafeshutdown_value;
	const char *size_value;
	const char *time_value;
	int cfg_size;
	int cfg_time;
	int was_enabled;
	int was_batchmode;
	int res=0;
	pthread_attr_t attr;

	ast_mutex_lock(&cdr_batch_lock);

	batchsize = BATCH_SIZE_DEFAULT;
	batchtime = BATCH_TIME_DEFAULT;
	batchscheduleronly = BATCH_SCHEDULER_ONLY_DEFAULT;
	batchsafeshutdown = BATCH_SAFE_SHUTDOWN_DEFAULT;
	was_enabled = enabled;
	was_batchmode = batchmode;
	enabled = 1;
	batchmode = 0;

	/* don't run the next scheduled CDR posting while reloading */
	if (cdr_sched > -1)
		ast_sched_del(sched, cdr_sched);

	if ((config = ast_config_load("cdr.conf"))) {
		if ((enabled_value = ast_variable_retrieve(config, "general", "enable"))) {
			enabled = ast_true(enabled_value);
		}
		if ((batched_value = ast_variable_retrieve(config, "general", "batch"))) {
			batchmode = ast_true(batched_value);
		}
		if ((scheduleronly_value = ast_variable_retrieve(config, "general", "scheduleronly"))) {
			batchscheduleronly = ast_true(scheduleronly_value);
		}
		if ((batchsafeshutdown_value = ast_variable_retrieve(config, "general", "safeshutdown"))) {
			batchsafeshutdown = ast_true(batchsafeshutdown_value);
		}
		if ((size_value = ast_variable_retrieve(config, "general", "size"))) {
			if (sscanf(size_value, "%d", &cfg_size) < 1)
				ast_log(LOG_WARNING, "Unable to convert '%s' to a numeric value.\n", size_value);
			else if (size_value < 0)
				ast_log(LOG_WARNING, "Invalid maximum batch size '%d' specified, using default\n", cfg_size);
			else
				batchsize = cfg_size;
		}
		if ((time_value = ast_variable_retrieve(config, "general", "time"))) {
			if (sscanf(time_value, "%d", &cfg_time) < 1)
				ast_log(LOG_WARNING, "Unable to convert '%s' to a numeric value.\n", time_value);
			else if (time_value < 0)
				ast_log(LOG_WARNING, "Invalid maximum batch time '%d' specified, using default\n", cfg_time);
			else
				batchtime = cfg_time;
		}
	}

	if (enabled && !batchmode) {
		ast_log(LOG_NOTICE, "CDR simple logging enabled.\n");
	} else if (enabled && batchmode) {
		cdr_sched = ast_sched_add(sched, batchtime * 1000, submit_scheduled_batch, NULL);
		ast_log(LOG_NOTICE, "CDR batch mode logging enabled, first of either size %d or time %d seconds.\n", batchsize, batchtime);
	} else {
		ast_log(LOG_NOTICE, "CDR logging disabled, data will be lost.\n");
	}

	/* if this reload enabled the CDR batch mode, create the background thread
	   if it does not exist */
	if (enabled && batchmode && (!was_enabled || !was_batchmode) && (cdr_thread == AST_PTHREADT_NULL)) {
		pthread_cond_init(&cdr_pending_cond, NULL);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (ast_pthread_create(&cdr_thread, &attr, do_cdr, NULL) < 0) {
			ast_log(LOG_ERROR, "Unable to start CDR thread.\n");
			ast_sched_del(sched, cdr_sched);
		} else {
			ast_cli_register(&cli_submit);
			ast_register_atexit(ast_cdr_engine_term);
			res = 0;
		}
	/* if this reload disabled the CDR and/or batch mode and there is a background thread,
	   kill it */
	} else if (((!enabled && was_enabled) || (!batchmode && was_batchmode)) && (cdr_thread != AST_PTHREADT_NULL)) {
		/* wake up the thread so it will exit */
		pthread_cancel(cdr_thread);
		pthread_kill(cdr_thread, SIGURG);
		pthread_join(cdr_thread, NULL);
		cdr_thread = AST_PTHREADT_NULL;
		pthread_cond_destroy(&cdr_pending_cond);
		ast_cli_unregister(&cli_submit);
		ast_unregister_atexit(ast_cdr_engine_term);
		res = 0;
		/* if leaving batch mode, then post the CDRs in the batch,
		   and don't reschedule, since we are stopping CDR logging */
		if (!batchmode && was_batchmode) {
			ast_cdr_engine_term();
		}
	} else {
		res = 0;
	}

	ast_mutex_unlock(&cdr_batch_lock);
	ast_config_destroy(config);

	return res;
}

int ast_cdr_engine_init(void)
{
	int res;

	sched = sched_context_create();
	if (!sched) {
		ast_log(LOG_ERROR, "Unable to create schedule context.\n");
		return -1;
	}

	ast_cli_register(&cli_status);

	res = do_reload();
	if (res) {
		ast_mutex_lock(&cdr_batch_lock);
		res = init_batch();
		ast_mutex_unlock(&cdr_batch_lock);
	}

	return res;
}

/* This actually gets called a couple of times at shutdown.  Once, before we start
   hanging up channels, and then again, after the channel hangup timeout expires */
void ast_cdr_engine_term(void)
{
	ast_cdr_submit_batch(batchsafeshutdown);
}

void ast_cdr_engine_reload(void)
{
	do_reload();
}

