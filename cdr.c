/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Call Detail Record API 
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 * Includes code and algorithms from the Zapata library.
 *
 */

#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/cdr.h>
#include <asterisk/logger.h>
#include <asterisk/callerid.h>
#include <asterisk/causes.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int ast_default_amaflags = AST_CDR_DOCUMENTATION;
char ast_default_accountcode[20] = "";

AST_MUTEX_DEFINE_STATIC(cdrlock);

static struct ast_cdr_beitem {
	char name[20];
	char desc[80];
	ast_cdrbe be;
	struct ast_cdr_beitem *next;
} *bes = NULL;


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
	ast_mutex_lock(&cdrlock);
	i = bes;
	while(i) {
		if (!strcasecmp(name, i->name))
			break;
		i = i->next;
	}
	ast_mutex_unlock(&cdrlock);
	if (i) {
		ast_log(LOG_WARNING, "Already have a CDR backend called '%s'\n", name);
		return -1;
	}
	i = malloc(sizeof(struct ast_cdr_beitem));
	if (!i) 	
		return -1;
	memset(i, 0, sizeof(struct ast_cdr_beitem));
	strncpy(i->name, name, sizeof(i->name) - 1);
	strncpy(i->desc, desc, sizeof(i->desc) - 1);
	i->be = be;
	ast_mutex_lock(&cdrlock);
	i->next = bes;
	bes = i;
	ast_mutex_unlock(&cdrlock);
	return 0;
}

void ast_cdr_unregister(char *name)
{
	struct ast_cdr_beitem *i, *prev = NULL;
	ast_mutex_lock(&cdrlock);
	i = bes;
	while(i) {
		if (!strcasecmp(name, i->name)) {
			if (prev)
				prev->next = i->next;
			else
				bes = i->next;
			break;
		}
		i = i->next;
	}
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Unregistered '%s' CDR backend\n", name);
	ast_mutex_unlock(&cdrlock);
	if (i) 
		free(i);
}

void ast_cdr_free(struct ast_cdr *cdr)
{
	char *chan;
	struct ast_cdr *next; 
	while (cdr) {
		next = cdr->next;
		chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
		if (!ast_cdr_has_flag(cdr,AST_CDR_FLAG_POSTED))
			ast_log(LOG_WARNING, "CDR on channel '%s' not posted\n", chan);
		if (!cdr->end.tv_sec && !cdr->end.tv_usec)
			ast_log(LOG_WARNING, "CDR on channel '%s' lacks end\n", chan);
		if (!cdr->start.tv_sec && !cdr->start.tv_usec)
			ast_log(LOG_WARNING, "CDR on channel '%s' lacks start\n", chan);
		free(cdr);
		cdr = next;
	}
}

struct ast_cdr *ast_cdr_alloc(void)
{
	struct ast_cdr *cdr;
	cdr = malloc(sizeof(struct ast_cdr));
	if (cdr) {
		memset(cdr, 0, sizeof(struct ast_cdr));
	}
	return cdr;
}

void ast_cdr_start(struct ast_cdr *cdr)
{
	char *chan; 
	while (cdr) {
		if (!ast_cdr_has_flag(cdr,AST_CDR_FLAG_LOCKED)) {
			chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (ast_cdr_has_flag(cdr,AST_CDR_FLAG_POSTED))
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
		if (ast_cdr_has_flag(cdr,AST_CDR_FLAG_POSTED))
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
		if (!ast_cdr_has_flag(cdr,AST_CDR_FLAG_LOCKED)) {
			chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (ast_cdr_has_flag(cdr,AST_CDR_FLAG_POSTED))
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
		if (ast_cdr_has_flag(cdr,AST_CDR_FLAG_POSTED))
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if(!ast_cdr_has_flag(cdr,AST_CDR_FLAG_LOCKED))
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
				ast_log(LOG_WARNING, "We don't handle that cause yet\n");
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
		if (ast_cdr_has_flag(cdr,AST_CDR_FLAG_POSTED))
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if(!ast_cdr_has_flag(cdr,AST_CDR_FLAG_LOCKED))
			strncpy(cdr->dstchannel, chann, sizeof(cdr->dstchannel) - 1);
		cdr = cdr->next;
	}
}

void ast_cdr_setapp(struct ast_cdr *cdr, char *app, char *data)
{
	char *chan; 
	while (cdr) {
		if(!ast_cdr_has_flag(cdr,AST_CDR_FLAG_LOCKED)) {
			chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (ast_cdr_has_flag(cdr,AST_CDR_FLAG_POSTED))
				ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
			if (!app)
				app = "";
			strncpy(cdr->lastapp, app, sizeof(cdr->lastapp) - 1);
			if (!data)
				data = "";
			strncpy(cdr->lastdata, data, sizeof(cdr->lastdata) - 1);
		}
		cdr = cdr->next;
	}
}

int ast_cdr_setcid(struct ast_cdr *cdr, struct ast_channel *c)
{
	char tmp[AST_MAX_EXTENSION] = "";
	char *num, *name;
	while (cdr) {
		if(!ast_cdr_has_flag(cdr,AST_CDR_FLAG_LOCKED)) {
			/* Grab source from ANI or normal Caller*ID */
			if (c->ani)
				strncpy(tmp, c->ani, sizeof(tmp) - 1);
			else if (c->callerid)
				strncpy(tmp, c->callerid, sizeof(tmp) - 1);
			if (c->callerid)
				strncpy(cdr->clid, c->callerid, sizeof(cdr->clid) - 1);
			name = NULL;
			num = NULL;
			ast_callerid_parse(tmp, &name, &num);
			if (num) {
				ast_shrink_phone_number(num);
				strncpy(cdr->src, num, sizeof(cdr->src) - 1);
			}
		}
		cdr = cdr->next;
	}
	return 0;
}

int ast_cdr_init(struct ast_cdr *cdr, struct ast_channel *c)
{
	char *chan;
	char *num, *name;
	char tmp[AST_MAX_EXTENSION] = "";
	while (cdr) {
		if(!ast_cdr_has_flag(cdr,AST_CDR_FLAG_LOCKED)) {
			chan = !ast_strlen_zero(cdr->channel) ? cdr->channel : "<unknown>";
			if (!ast_strlen_zero(cdr->channel)) 
				ast_log(LOG_WARNING, "CDR already initialized on '%s'\n", chan); 
			strncpy(cdr->channel, c->name, sizeof(cdr->channel) - 1);
			/* Grab source from ANI or normal Caller*ID */
			if (c->ani)
				strncpy(tmp, c->ani, sizeof(tmp) - 1);
			else if (c->callerid)
				strncpy(tmp, c->callerid, sizeof(tmp) - 1);
			if (c->callerid)
				strncpy(cdr->clid, c->callerid, sizeof(cdr->clid) - 1);
			name = NULL;
			num = NULL;
			ast_callerid_parse(tmp, &name, &num);
			if (num) {
				ast_shrink_phone_number(num);
				strncpy(cdr->src, num, sizeof(cdr->src) - 1);
			}
			
			if (c->_state == AST_STATE_UP)
				cdr->disposition = AST_CDR_ANSWERED;
			else
				cdr->disposition = AST_CDR_NOANSWER;
			if (c->amaflags)
				cdr->amaflags = c->amaflags;
			else
				cdr->amaflags = ast_default_amaflags;
			strncpy(cdr->accountcode, c->accountcode, sizeof(cdr->accountcode) - 1);
			/* Destination information */
			strncpy(cdr->dst, c->exten, sizeof(cdr->dst) - 1);
			strncpy(cdr->dcontext, c->context, sizeof(cdr->dcontext) - 1);
			/* Unique call identifier */
			strncpy(cdr->uniqueid, c->uniqueid, sizeof(cdr->uniqueid) - 1);
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
		if (ast_cdr_has_flag(cdr,AST_CDR_FLAG_POSTED))
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
	default:
		return "UNKNOWN";
	}
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

int ast_cdr_setaccount(struct ast_channel *chan, char *account)
{
	struct ast_cdr *cdr = chan->cdr;

	strncpy(chan->accountcode, account, sizeof(chan->accountcode) - 1);
	while (cdr) {
		if(!ast_cdr_has_flag(cdr,AST_CDR_FLAG_LOCKED))
			strncpy(cdr->accountcode, chan->accountcode, sizeof(cdr->accountcode) - 1);
		cdr = cdr->next;
	}
	return 0;
}

int ast_cdr_setamaflags(struct ast_channel *chan, char *flag)
{
	struct ast_cdr *cdr = chan->cdr;
	int newflag;

	newflag = ast_cdr_amaflags2int(flag);
	if (newflag) {
		cdr->amaflags = newflag;
	}
	return 0;
}

int ast_cdr_setuserfield(struct ast_channel *chan, char *userfield)
{
	struct ast_cdr *cdr = chan->cdr;

	while (cdr) {
		if(!ast_cdr_has_flag(cdr,AST_CDR_FLAG_LOCKED)) 
			strncpy(cdr->userfield, userfield, sizeof(cdr->userfield) - 1);
		cdr = cdr->next;
	}
	return 0;
}

int ast_cdr_appenduserfield(struct ast_channel *chan, char *userfield)
{
	struct ast_cdr *cdr = chan->cdr;

	while (cdr)
	{

		int len = strlen(cdr->userfield);
		if(!ast_cdr_has_flag(cdr,AST_CDR_FLAG_LOCKED))
			strncpy(cdr->userfield+len, userfield, sizeof(cdr->userfield) - len - 1);
		cdr = cdr->next;
	}
	return 0;
}

int ast_cdr_update(struct ast_channel *c)
{
	struct ast_cdr *cdr = c->cdr;
	char *name, *num;
	char tmp[AST_MAX_EXTENSION] = "";
	/* Grab source from ANI or normal Caller*ID */
	while (cdr) {
		if(!ast_cdr_has_flag(cdr,AST_CDR_FLAG_LOCKED)) {
			if (c->ani)
				strncpy(tmp, c->ani, sizeof(tmp) - 1);
			else if (c->callerid && !ast_strlen_zero(c->callerid))
				strncpy(tmp, c->callerid, sizeof(tmp) - 1);
			if (c->callerid && !ast_strlen_zero(c->callerid))
				strncpy(cdr->clid, c->callerid, sizeof(cdr->clid) - 1);
			else
				cdr->clid[0] = '\0';
			name = NULL;
			num = NULL;
			ast_callerid_parse(tmp, &name, &num);
			if (num) {
				ast_shrink_phone_number(num);
				strncpy(cdr->src, num, sizeof(cdr->src) - 1);
			}
			/* Copy account code et-al */	
			strncpy(cdr->accountcode, c->accountcode, sizeof(cdr->accountcode) - 1);
			/* Destination information */
			strncpy(cdr->dst, c->exten, sizeof(cdr->dst) - 1);
			strncpy(cdr->dcontext, c->context, sizeof(cdr->dcontext) - 1);
		}
		cdr = cdr->next;
	}

	return 0;
}

int ast_cdr_amaflags2int(char *flag)
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
		if (ast_cdr_has_flag(cdr,AST_CDR_FLAG_POSTED))
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (!cdr->end.tv_sec && !cdr->end.tv_usec)
			ast_log(LOG_WARNING, "CDR on channel '%s' lacks end\n", chan);
		if (!cdr->start.tv_sec && !cdr->start.tv_usec)
			ast_log(LOG_WARNING, "CDR on channel '%s' lacks start\n", chan);
		cdr->duration = cdr->end.tv_sec - cdr->start.tv_sec + (cdr->end.tv_usec - cdr->start.tv_usec) / 1000000;
		if (cdr->answer.tv_sec || cdr->answer.tv_usec) {
			cdr->billsec = cdr->end.tv_sec - cdr->answer.tv_sec + (cdr->end.tv_usec - cdr->answer.tv_usec) / 1000000;
		} else
			cdr->billsec = 0;
		ast_cdr_add_flag(cdr,AST_CDR_FLAG_POSTED);
		ast_mutex_lock(&cdrlock);
		i = bes;
		while(i) {
			i->be(cdr);
			i = i->next;
		}
		ast_mutex_unlock(&cdrlock);
		cdr = cdr->next;
	}
}

void ast_cdr_reset(struct ast_cdr *cdr, int post)
{
	while (cdr) {
		/* Post if requested */
		if (post) {
			ast_cdr_end(cdr);
			ast_cdr_post(cdr);
		}
		/* Reset to initial state */
		cdr->flags=0;
		memset(&cdr->start, 0, sizeof(cdr->start));
		memset(&cdr->end, 0, sizeof(cdr->end));
		memset(&cdr->answer, 0, sizeof(cdr->answer));
		cdr->billsec = 0;
		cdr->duration = 0;
		ast_cdr_start(cdr);
		cdr->disposition = AST_CDR_NOANSWER;
		cdr = cdr->next;
	}
}

void ast_cdr_append(struct ast_cdr *cdr, struct ast_cdr *newcdr) {
	if (cdr) {
		while(cdr->next)
			cdr = cdr->next;
		cdr->next = newcdr;
	} else
		ast_log(LOG_ERROR, "Can't append a CDR to NULL!\n");
}
