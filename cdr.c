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

#include <asterisk/channel.h>
#include <asterisk/cdr.h>
#include <asterisk/logger.h>
#include <asterisk/callerid.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

int ast_default_amaflags = AST_CDR_DOCUMENTATION;
char ast_default_accountcode[20] = "";

static pthread_mutex_t cdrlock = PTHREAD_MUTEX_INITIALIZER;

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
	ast_pthread_mutex_lock(&cdrlock);
	i = bes;
	while(i) {
		if (!strcasecmp(name, i->name))
			break;
		i = i->next;
	}
	ast_pthread_mutex_unlock(&cdrlock);
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
	ast_pthread_mutex_lock(&cdrlock);
	i->next = bes;
	bes = i;
	ast_pthread_mutex_unlock(&cdrlock);
	return 0;
}

void ast_cdr_unregister(char *name)
{
	struct ast_cdr_beitem *i, *prev = NULL;
	ast_pthread_mutex_lock(&cdrlock);
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
	ast_pthread_mutex_unlock(&cdrlock);
	if (i) 
		free(i);
}

void ast_cdr_free(struct ast_cdr *cdr)
{
	char *chan; 
	if (cdr) {
		chan = strlen(cdr->channel) ? cdr->channel : "<unknown>";
		if (!cdr->posted)
			ast_log(LOG_WARNING, "CDR on channel '%s' not posted\n", chan);
		if (!cdr->end.tv_sec && !cdr->end.tv_usec)
			ast_log(LOG_WARNING, "CDR on channel '%s' lacks end\n", chan);
		if (!cdr->start.tv_sec && !cdr->start.tv_usec)
			ast_log(LOG_WARNING, "CDR on channel '%s' lacks start\n", chan);
		free(cdr);
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
	if (cdr) {
		chan = strlen(cdr->channel) ? cdr->channel : "<unknown>";
		if (cdr->posted)
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (cdr->start.tv_sec || cdr->start.tv_usec)
			ast_log(LOG_WARNING, "CDR on channel '%s' already started\n", chan);
		gettimeofday(&cdr->start, NULL);
	}
}

void ast_cdr_answer(struct ast_cdr *cdr)
{
	char *chan; 
	if (cdr) {
		chan = strlen(cdr->channel) ? cdr->channel : "<unknown>";
		if (cdr->posted)
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (cdr->disposition < AST_CDR_ANSWERED)
			cdr->disposition = AST_CDR_ANSWERED;
		if (!cdr->answer.tv_sec && !cdr->answer.tv_usec) {
			gettimeofday(&cdr->answer, NULL);
		}
	}
}

void ast_cdr_busy(struct ast_cdr *cdr)
{
	char *chan; 
	if (cdr) {
		chan = strlen(cdr->channel) ? cdr->channel : "<unknown>";
		if (cdr->posted)
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (cdr->disposition < AST_CDR_BUSY)
			cdr->disposition = AST_CDR_BUSY;
	}
}

void ast_cdr_setdestchan(struct ast_cdr *cdr, char *chann)
{
	char *chan; 
	if (cdr) {
		chan = strlen(cdr->channel) ? cdr->channel : "<unknown>";
		if (cdr->posted)
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		strncpy(cdr->dstchannel, chann, sizeof(cdr->dstchannel) - 1);
	}
}

void ast_cdr_setapp(struct ast_cdr *cdr, char *app, char *data)
{
	char *chan; 
	if (cdr) {
		chan = strlen(cdr->channel) ? cdr->channel : "<unknown>";
		if (cdr->posted)
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (!app)
			app = "";
		strncpy(cdr->lastapp, app, sizeof(cdr->lastapp) - 1);
		if (!data)
			data = "";
		strncpy(cdr->lastdata, data, sizeof(cdr->lastdata) - 1);
	}
}

int ast_cdr_init(struct ast_cdr *cdr, struct ast_channel *c)
{
	char *chan;
	char *num, *name;
	char tmp[AST_MAX_EXTENSION];
	if (cdr) {
		chan = strlen(cdr->channel) ? cdr->channel : "<unknown>";
		if (strlen(cdr->channel)) 
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
		
		if (c->state == AST_STATE_UP)
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
	}
	return 0;
}

void ast_cdr_end(struct ast_cdr *cdr)
{
	char *chan;
	if (cdr) {
		chan = strlen(cdr->channel) ? cdr->channel : "<unknown>";
		if (cdr->posted)
			ast_log(LOG_WARNING, "CDR on channel '%s' already posted\n", chan);
		if (!cdr->start.tv_sec && !cdr->start.tv_usec)
			ast_log(LOG_WARNING, "CDR on channel '%s' has not started\n", chan);
		if (!cdr->end.tv_sec && !cdr->end.tv_usec) 
			gettimeofday(&cdr->end, NULL);
	}
}

char *ast_cdr_disp2str(int disposition)
{
	switch (disposition) {
	case AST_CDR_NOANSWER:
		return "NO ANSWER";
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
	if (cdr) {
		chan = strlen(cdr->channel) ? cdr->channel : "<unknown>";
		if (cdr->posted)
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
		cdr->posted = 1;
		ast_pthread_mutex_lock(&cdrlock);
		i = bes;
		while(i) {
			i->be(cdr);
			i = i->next;
		}
		ast_pthread_mutex_unlock(&cdrlock);
	}
}
