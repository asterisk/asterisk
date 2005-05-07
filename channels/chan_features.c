/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * feature Proxy Channel
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <string.h>
#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/lock.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/rtp.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/musiconhold.h"
#include "asterisk/manager.h"
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/signal.h>


static const char desc[] = "Feature Proxy Channel";
static const char type[] = "Feature";
static const char tdesc[] = "Feature Proxy Channel Driver";

static int usecnt =0;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);

#define IS_OUTBOUND(a,b) (a == b->chan ? 1 : 0)

/* Protect the interface list (of feature_pvt's) */
AST_MUTEX_DEFINE_STATIC(featurelock);

struct feature_sub {
	struct ast_channel *owner;
	int inthreeway;
	int pfd;
	int timingfdbackup;
	int alertpipebackup[2];
};

static struct feature_pvt {
	ast_mutex_t lock;			/* Channel private lock */
	char tech[AST_MAX_EXTENSION];		/* Technology to abstract */
	char dest[AST_MAX_EXTENSION];		/* Destination to abstract */
	struct ast_channel *subchan;
	struct feature_sub subs[3];		/* Subs */
	struct ast_channel *owner;		/* Current Master Channel */
	struct feature_pvt *next;		/* Next entity */
} *features = NULL;

#define SUB_REAL	0			/* Active call */
#define SUB_CALLWAIT	1			/* Call-Waiting call on hold */
#define SUB_THREEWAY	2			/* Three-way call */

static struct ast_channel *features_request(const char *type, int format, void *data, int *cause);
static int features_digit(struct ast_channel *ast, char digit);
static int features_call(struct ast_channel *ast, char *dest, int timeout);
static int features_hangup(struct ast_channel *ast);
static int features_answer(struct ast_channel *ast);
static struct ast_frame *features_read(struct ast_channel *ast);
static int features_write(struct ast_channel *ast, struct ast_frame *f);
static int features_indicate(struct ast_channel *ast, int condition);
static int features_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);

static const struct ast_channel_tech features_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = -1,
	.requester = features_request,
	.send_digit = features_digit,
	.call = features_call,
	.hangup = features_hangup,
	.answer = features_answer,
	.read = features_read,
	.write = features_write,
	.exception = features_read,
	.indicate = features_indicate,
	.fixup = features_fixup,
};

static inline void init_sub(struct feature_sub *sub)
{
	sub->inthreeway = 0;
	sub->pfd = -1;
	sub->timingfdbackup = -1;
	sub->alertpipebackup[0] = sub->alertpipebackup[1] = -1;
}

static inline int indexof(struct feature_pvt *p, struct ast_channel *owner, int nullok)
{
	int x;
	if (!owner) {
		ast_log(LOG_WARNING, "indexof called on NULL owner??\n");
		return -1;
	}
	for (x=0;x<3;x++) {
		if (owner == p->subs[x].owner)
			return x;
	}
	return -1;
}

#if 0
static void wakeup_sub(struct feature_pvt *p, int a)
{
	struct ast_frame null = { AST_FRAME_NULL, };
	for (;;) {
		if (p->subs[a].owner) {
			if (ast_mutex_trylock(&p->subs[a].owner->lock)) {
				ast_mutex_unlock(&p->lock);
				usleep(1);
				ast_mutex_lock(&p->lock);
			} else {
				ast_queue_frame(p->subs[a].owner, &null);
				ast_mutex_unlock(&p->subs[a].owner->lock);
				break;
			}
		} else
			break;
	}
}
#endif

static void restore_channel(struct feature_pvt *p, int index)
{
	/* Restore timing/alertpipe */
	p->subs[index].owner->timingfd = p->subs[index].timingfdbackup;
	p->subs[index].owner->alertpipe[0] = p->subs[index].alertpipebackup[0];
	p->subs[index].owner->alertpipe[1] = p->subs[index].alertpipebackup[1];
	p->subs[index].owner->fds[AST_MAX_FDS-1] = p->subs[index].alertpipebackup[0];
	p->subs[index].owner->fds[AST_MAX_FDS-2] = p->subs[index].timingfdbackup;
}

static void update_features(struct feature_pvt *p, int index)
{
	int x;
	if (p->subs[index].owner) {
		for (x=0;x<AST_MAX_FDS;x++) {
			if (index) 
				p->subs[index].owner->fds[x] = -1;
			else
				p->subs[index].owner->fds[x] = p->subchan->fds[x];
		}
		if (!index) {
			/* Copy timings from master channel */
			p->subs[index].owner->timingfd = p->subchan->timingfd;
			p->subs[index].owner->alertpipe[0] = p->subchan->alertpipe[0];
			p->subs[index].owner->alertpipe[1] = p->subchan->alertpipe[1];
			if (p->subs[index].owner->nativeformats != p->subchan->readformat) {
				p->subs[index].owner->nativeformats = p->subchan->readformat;
				if (p->subs[index].owner->readformat)
					ast_set_read_format(p->subs[index].owner, p->subs[index].owner->readformat);
				if (p->subs[index].owner->writeformat)
					ast_set_write_format(p->subs[index].owner, p->subs[index].owner->writeformat);
			}
		} else{
			restore_channel(p, index);
		}
	}
}

#if 0
static void swap_subs(struct feature_pvt *p, int a, int b)
{
	int tinthreeway;
	struct ast_channel *towner;

	ast_log(LOG_DEBUG, "Swapping %d and %d\n", a, b);

	towner = p->subs[a].owner;
	tinthreeway = p->subs[a].inthreeway;

	p->subs[a].owner = p->subs[b].owner;
	p->subs[a].inthreeway = p->subs[b].inthreeway;

	p->subs[b].owner = towner;
	p->subs[b].inthreeway = tinthreeway;
	update_features(p,a);
	update_features(p,b);
	wakeup_sub(p, a);
	wakeup_sub(p, b);
}
#endif

static int features_answer(struct ast_channel *ast)
{
	struct feature_pvt *p = ast->tech_pvt;
	int res = -1;
	int x;
	ast_mutex_lock(&p->lock);
	x = indexof(p, ast, 0);
	if (!x && p->subchan)
		res = ast_answer(p->subchan);
	ast_mutex_unlock(&p->lock);
	return res;
}

static struct ast_frame  *features_read(struct ast_channel *ast)
{
	static struct ast_frame null_frame = { AST_FRAME_NULL, };
	struct feature_pvt *p = ast->tech_pvt;
	struct ast_frame *f;
	int x;
	
	f = &null_frame;
	ast_mutex_lock(&p->lock);
	x = indexof(p, ast, 0);
	if (!x && p->subchan) {
		update_features(p, x);
		f = ast_read(p->subchan);
	}
	ast_mutex_unlock(&p->lock);
	return f;
}

static int features_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct feature_pvt *p = ast->tech_pvt;
	int res = -1;
	int x;
	ast_mutex_lock(&p->lock);
	x = indexof(p, ast, 0);
	if (!x && p->subchan)
		res = ast_write(p->subchan, f);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int features_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct feature_pvt *p = newchan->tech_pvt;
	int x;
	ast_mutex_lock(&p->lock);
	if (p->owner == oldchan)
		p->owner = newchan;
	for (x=0;x<3;x++) {
		if (p->subs[x].owner == oldchan)
			p->subs[x].owner = newchan;
	}
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int features_indicate(struct ast_channel *ast, int condition)
{
	struct feature_pvt *p = ast->tech_pvt;
	int res = -1;
	int x;
	/* Queue up a frame representing the indication as a control frame */
	ast_mutex_lock(&p->lock);
	x = indexof(p, ast, 0);
	if (!x && p->subchan)
		res = ast_indicate(p->subchan, condition);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int features_digit(struct ast_channel *ast, char digit)
{
	struct feature_pvt *p = ast->tech_pvt;
	int res = -1;
	int x;
	/* Queue up a frame representing the indication as a control frame */
	ast_mutex_lock(&p->lock);
	x = indexof(p, ast, 0);
	if (!x && p->subchan)
		res = ast_senddigit(p->subchan, digit);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int features_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct feature_pvt *p = ast->tech_pvt;
	int res = -1;
	int x;
	char *dest2;
		
	dest2 = strchr(dest, '/');
	if (dest2) {
		ast_mutex_lock(&p->lock);
		x = indexof(p, ast, 0);
		if (!x && p->subchan) {
			if (p->owner->cid.cid_num)
				p->subchan->cid.cid_num = strdup(p->owner->cid.cid_num);
			else 
				p->subchan->cid.cid_num = NULL;
		
			if (p->owner->cid.cid_name)
				p->subchan->cid.cid_name = strdup(p->owner->cid.cid_name);
			else 
				p->subchan->cid.cid_name = NULL;
		
			if (p->owner->cid.cid_rdnis)
				p->subchan->cid.cid_rdnis = strdup(p->owner->cid.cid_rdnis);
			else
				p->subchan->cid.cid_rdnis = NULL;
		
			if (p->owner->cid.cid_ani)
				p->subchan->cid.cid_ani = strdup(p->owner->cid.cid_ani);
			else
				p->subchan->cid.cid_ani = NULL;
		
			strncpy(p->subchan->language, p->owner->language, sizeof(p->subchan->language) - 1);
			strncpy(p->subchan->accountcode, p->owner->accountcode, sizeof(p->subchan->accountcode) - 1);
			p->subchan->cdrflags = p->owner->cdrflags;
			res = ast_call(p->subchan, dest2, timeout);
			update_features(p, x);
		} else
			ast_log(LOG_NOTICE, "Uhm yah, not quite there with the call waiting...\n");
		ast_mutex_unlock(&p->lock);
	}
	return res;
}

static int features_hangup(struct ast_channel *ast)
{
	struct feature_pvt *p = ast->tech_pvt;
	struct feature_pvt *cur, *prev=NULL;
	int x;

	ast_mutex_lock(&p->lock);
	x = indexof(p, ast, 0);
	if (x > -1) {
		restore_channel(p, x);
		p->subs[x].owner = NULL;
		/* XXX Re-arrange, unconference, etc XXX */
	}
	ast->tech_pvt = NULL;
	
	
	if (!p->subs[SUB_REAL].owner && !p->subs[SUB_CALLWAIT].owner && !p->subs[SUB_THREEWAY].owner) {
		ast_mutex_unlock(&p->lock);
		/* Remove from list */
		ast_mutex_lock(&featurelock);
		cur = features;
		while(cur) {
			if (cur == p) {
				if (prev)
					prev->next = cur->next;
				else
					features = cur->next;
				break;
			}
			prev = cur;
			cur = cur->next;
		}
		ast_mutex_unlock(&featurelock);
		ast_mutex_lock(&p->lock);
		/* And destroy */
		if (p->subchan)
			ast_hangup(p->subchan);
		ast_mutex_unlock(&p->lock);
		ast_mutex_destroy(&p->lock);
		free(p);
		return 0;
	}
	ast_mutex_unlock(&p->lock);
	return 0;
}

static struct feature_pvt *features_alloc(char *data, int format)
{
	struct feature_pvt *tmp;
	char *dest=NULL;
	char *tech;
	int x;
	int status;
	struct ast_channel *chan;
	
	tech = ast_strdupa(data);
	if (tech) {
		dest = strchr(tech, '/');
		if (dest) {
			*dest = '\0';
			dest++;
		}
	}
	if (!tech || !dest) {
		ast_log(LOG_NOTICE, "Format for feature channel is Feature/Tech/Dest ('%s' not valid)!\n", 
			data);
		return NULL;
	}
	ast_mutex_lock(&featurelock);
	tmp = features;
	while(tmp) {
		if (!strcasecmp(tmp->tech, tech) && !strcmp(tmp->dest, dest))
			break;
		tmp = tmp->next;
	}
	ast_mutex_unlock(&featurelock);
	if (!tmp) {
		chan = ast_request(tech, format, dest, &status);
		if (!chan) {
			ast_log(LOG_NOTICE, "Unable to allocate subchannel '%s/%s'\n", tech, dest);
			return NULL;
		}
		tmp = malloc(sizeof(struct feature_pvt));
		if (tmp) {
			memset(tmp, 0, sizeof(struct feature_pvt));
			for (x=0;x<3;x++)
				init_sub(tmp->subs + x);
			ast_mutex_init(&tmp->lock);
			strncpy(tmp->tech, tech, sizeof(tmp->tech) - 1);
			strncpy(tmp->dest, dest, sizeof(tmp->dest) - 1);
			tmp->subchan = chan;
			ast_mutex_lock(&featurelock);
			tmp->next = features;
			features = tmp;
			ast_mutex_unlock(&featurelock);
		}
	}
	return tmp;
}

static struct ast_channel *features_new(struct feature_pvt *p, int state, int index)
{
	struct ast_channel *tmp;
	int x,y;
	if (!p->subchan) {
		ast_log(LOG_WARNING, "Called upon channel with no subchan:(\n");
		return NULL;
	}
	if (p->subs[index].owner) {
		ast_log(LOG_WARNING, "Called to put index %d already there!\n", index);
		return NULL;
	}
	tmp = ast_channel_alloc(0);
	if (!tmp) {
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
		return NULL;
	}
	tmp->tech = &features_tech;
	for (x=1;x<4;x++) {
		snprintf(tmp->name, sizeof(tmp->name), "Feature/%s/%s-%d", p->tech, p->dest, x);
		for (y=0;y<3;y++) {
			if (y == index)
				continue;
			if (p->subs[x].owner && !strcasecmp(p->subs[x].owner->name, tmp->name))
				break;
		}
		if (y >= 3)
			break;
	}
	tmp->type = type;
	ast_setstate(tmp, state);
	tmp->writeformat = p->subchan->writeformat;
	tmp->rawwriteformat = p->subchan->rawwriteformat;
	tmp->readformat = p->subchan->readformat;
	tmp->rawreadformat = p->subchan->rawreadformat;
	tmp->nativeformats = p->subchan->readformat;
	tmp->tech_pvt = p;
	p->subs[index].owner = tmp;
	if (!p->owner)
		p->owner = tmp;
	ast_mutex_lock(&usecnt_lock);
	usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
	return tmp;
}


static struct ast_channel *features_request(const char *type, int format, void *data, int *cause)
{
	struct feature_pvt *p;
	struct ast_channel *chan = NULL;
	p = features_alloc(data, format);
	if (p && !p->subs[SUB_REAL].owner)
		chan = features_new(p, AST_STATE_DOWN, SUB_REAL);
	if (chan)
		update_features(p,SUB_REAL);
	return chan;
}

static int features_show(int fd, int argc, char **argv)
{
	struct feature_pvt *p;

	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&featurelock);
	p = features;
	while(p) {
		ast_mutex_lock(&p->lock);
		ast_cli(fd, "%s -- %s/%s\n", p->owner ? p->owner->name : "<unowned>", p->tech, p->dest);
		ast_mutex_unlock(&p->lock);
		p = p->next;
	}
	if (!features)
		ast_cli(fd, "No feature channels in use\n");
	ast_mutex_unlock(&featurelock);
	return RESULT_SUCCESS;
}

static char show_features_usage[] = 
"Usage: feature show channels\n"
"       Provides summary information on feature channels.\n";

static struct ast_cli_entry cli_show_features = {
	{ "feature", "show", "channels", NULL }, features_show, 
	"Show status of feature channels", show_features_usage, NULL };

int load_module()
{
	/* Make sure we can register our sip channel type */
	if (ast_channel_register(&features_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	ast_cli_register(&cli_show_features);
	return 0;
}

int reload()
{
	return 0;
}

int unload_module()
{
	struct feature_pvt *p;
	/* First, take us out of the channel loop */
	ast_cli_unregister(&cli_show_features);
	ast_channel_unregister(&features_tech);
	if (!ast_mutex_lock(&featurelock)) {
		/* Hangup all interfaces if they have an owner */
		p = features;
		while(p) {
			if (p->owner)
				ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
			p = p->next;
		}
		features = NULL;
		ast_mutex_unlock(&featurelock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}		
	return 0;
}

int usecount()
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

char *description()
{
	return (char *) desc;
}

