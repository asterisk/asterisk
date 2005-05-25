/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Call Detail Record API 
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 * Includes code and algorithms from the Zapata library.
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/logger.h"
#include "asterisk/callerid.h"
#include "asterisk/causes.h"
#include "asterisk/options.h"
#include "asterisk/linkedlists.h"
#include "asterisk/utils.h"

int ast_default_amaflags = AST_CDR_DOCUMENTATION;
char ast_default_accountcode[AST_MAX_ACCOUNT_CODE] = "";

struct ast_cdr_beitem {
	char name[20];
	char desc[80];
	ast_cdrbe be;
	AST_LIST_ENTRY(ast_cdr_beitem) list;
};

static AST_LIST_HEAD_STATIC(be_list, ast_cdr_beitem);

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

static const char *ast_cdr_getvar_internal(struct ast_cdr *cdr, const char *name, int recur) 
{
	struct ast_var_t *variables;
	struct varshead *headp;

	if (!name || ast_strlen_zero(name))
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
		t = cdr->start.tv_sec;
		if (t) {
			localtime_r(&t, &tm);
			strftime(workspace, workspacelen, fmt, &tm);
		}
	} else if (!strcasecmp(name, "end")) {
		t = cdr->start.tv_sec;
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

#define CDR_CLEN 18
int ast_cdr_serialize_variables(struct ast_cdr *cdr, char *buf, size_t size, char delim, char sep, int recur) 
{
	struct ast_var_t *variables;
	struct varshead *headp;
	char *var=NULL ,*val=NULL;
	char *tmp = NULL;
	char workspace[256];
	int workspacelen;
	int total = 0, x = 0, i = 0;
	const char *cdrcols[CDR_CLEN] = { 
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


	memset(buf,0,size);
	while (cdr) {
		x++;
		if (x > 1) {
			strncat(buf, "\n", size);
		}
		headp = &cdr->varshead;
		AST_LIST_TRAVERSE(headp,variables,entries) {
			if (cdr && variables &&
			    (var = ast_var_name(variables)) && (val = ast_var_value(variables)) &&
			    !ast_strlen_zero(var) && !ast_strlen_zero(val)) {
				snprintf(buf + strlen(buf), size - strlen(buf), "level %d: %s%c%s%c", x, var, delim, val, sep);
				if (strlen(buf) >= size) {
					ast_log(LOG_ERROR,"Data Buffer Size Exceeded!\n");
					break;
				}
				total++;
			} else 
				break;
		}
		for (i = 0 ; i < CDR_CLEN; i++) {
			workspacelen = sizeof(workspace);
			ast_cdr_getvar(cdr, cdrcols[i], &tmp, workspace, workspacelen, 0);
			if (!tmp)
				continue;
			
			snprintf(buf + strlen(buf), size - strlen(buf), "level %d: %s%c%s%c", x, cdrcols[i], delim, tmp, sep);
			if (strlen(buf) >= size) {
				ast_log(LOG_ERROR,"Data Buffer Size Exceeded!\n");
				break;
			}
			total++;
		}

		if (!recur) {
			break;
		}

		cdr = cdr->next;
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
		if (!ast_test_flag(cdr, AST_CDR_FLAG_POSTED))
			ast_log(LOG_WARNING, "CDR on channel '%s' not posted\n", chan);
		if (!cdr->end.tv_sec && !cdr->end.tv_usec)
			ast_log(LOG_WARNING, "CDR on channel '%s' lacks end\n", chan);
		if (!cdr->start.tv_sec && !cdr->start.tv_usec)
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
			if (cdr->start.tv_sec || cdr->start.tv_usec)
				ast_log(LOG_WARNING, "CDR on channel '%s' already started\n", chan);
			gettimeofday(&cdr->start, NULL);
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
		if (!cdr->answer.tv_sec && !cdr->answer.tv_usec) {
			gettimeofday(&cdr->answer, NULL);
		}
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
				strncpy(tmp, c->cid.cid_name, sizeof(tmp) - 1);
			else if (num)
				strncpy(tmp, num, sizeof(tmp) - 1);
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
				strncpy(tmp, c->cid.cid_name, sizeof(tmp) - 1);
			else if (num)
				strncpy(tmp, num, sizeof(tmp) - 1);
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
		if (!cdr->start.tv_sec && !cdr->start.tv_usec)
			ast_log(LOG_WARNING, "CDR on channel '%s' has not started\n", chan);
		if (!cdr->end.tv_sec && !cdr->end.tv_usec) 
			gettimeofday(&cdr->end, NULL);
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
				strncpy(tmp, c->cid.cid_name, sizeof(tmp) - 1);
			else if (num)
				strncpy(tmp, num, sizeof(tmp) - 1);
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

void ast_cdr_post(struct ast_cdr *cdr)
{
	char *chan;
	struct ast_cdr_beitem *i;

	while (cdr) {
		chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (ast_test_flag(cdr, AST_CDR_FLAG_POSTED))
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (!cdr->end.tv_sec && !cdr->end.tv_usec)
			ast_log(LOG_WARNING, "CDR on channel '%s' lacks end\n", chan);
		if (!cdr->start.tv_sec && !cdr->start.tv_usec)
			ast_log(LOG_WARNING, "CDR on channel '%s' lacks start\n", chan);
		cdr->duration = cdr->end.tv_sec - cdr->start.tv_sec + (cdr->end.tv_usec - cdr->start.tv_usec) / 1000000;
		if (cdr->answer.tv_sec || cdr->answer.tv_usec)
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

	while (cdr) {
		/* Post if requested */
		if (ast_test_flag(&tmp, AST_CDR_FLAG_LOCKED) || !ast_test_flag(cdr, AST_CDR_FLAG_LOCKED)) {
			if (ast_test_flag(&tmp, AST_CDR_FLAG_POSTED)) {
				ast_cdr_end(cdr);
				ast_cdr_post(cdr);
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
