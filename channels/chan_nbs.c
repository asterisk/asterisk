/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Generic Linux Telephony Interface driver
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
#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <nbs.h>

static char *desc = "Network Broadcast Sound Support";
static char *type = "NBS";
static char *tdesc = "Network Broadcast Sound Driver";

static int usecnt =0;

/* Only linear is allowed */
static int prefformat = AST_FORMAT_SLINEAR;

AST_MUTEX_DEFINE_STATIC(usecnt_lock);

static char context[AST_MAX_EXTENSION] = "default";

/* NBS creates private structures on demand */
   
struct nbs_pvt {
	NBS *nbs;
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	char app[16];					/* Our app */
	char stream[80];				/* Our stream */
	struct ast_frame fr;			/* "null" frame */
};

static int nbs_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct nbs_pvt *p;

	p = ast->pvt->pvt;

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "nbs_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	if (option_debug)
		ast_log(LOG_DEBUG, "Calling %s on %s\n", dest, ast->name);

	/* If we can't connect, return congestion */
	if (nbs_connect(p->nbs)) {
		ast_log(LOG_WARNING, "NBS Connection failed on %s\n", ast->name);
		ast_queue_control(ast, AST_CONTROL_CONGESTION);
	} else {
		ast_setstate(ast, AST_STATE_RINGING);
		ast_queue_control(ast, AST_CONTROL_ANSWER);
	}

	return 0;
}

static void nbs_destroy(struct nbs_pvt *p)
{
	if (p->nbs)
		nbs_delstream(p->nbs);
	free(p);
}

static struct nbs_pvt *nbs_alloc(void *data)
{
	struct nbs_pvt *p;
	int flags = 0;
	char stream[256] = "";
	char *opts;
	strncpy(stream, data, sizeof(stream) - 1);
	if ((opts = strchr(stream, ':'))) {
		*opts = '\0';
		opts++;
	} else
		opts = "";
	p = malloc(sizeof(struct nbs_pvt));
	if (p) {
		memset(p, 0, sizeof(struct nbs_pvt));
		if (strlen(opts)) {
			if (strchr(opts, 'm'))
				flags |= NBS_FLAG_MUTE;
			if (strchr(opts, 'o'))
				flags |= NBS_FLAG_OVERSPEAK;
			if (strchr(opts, 'e'))
				flags |= NBS_FLAG_EMERGENCY;
			if (strchr(opts, 'O'))
				flags |= NBS_FLAG_OVERRIDE;
		} else
			flags = NBS_FLAG_OVERSPEAK;
		
		strncpy(p->stream, stream, sizeof(p->stream) - 1);
		p->nbs = nbs_newstream("asterisk", stream, flags);
		if (!p->nbs) {
			ast_log(LOG_WARNING, "Unable to allocate new NBS stream '%s' with flags %d\n", stream, flags);
			free(p);
			p = NULL;
		} else {
			/* Set for 8000 hz mono, 640 samples */
			nbs_setbitrate(p->nbs, 8000);
			nbs_setchannels(p->nbs, 1);
			nbs_setblocksize(p->nbs, 640);
			nbs_setblocking(p->nbs, 0);
		}
	}
	return p;
}

static int nbs_hangup(struct ast_channel *ast)
{
	struct nbs_pvt *p;
	p = ast->pvt->pvt;
	if (option_debug)
		ast_log(LOG_DEBUG, "nbs_hangup(%s)\n", ast->name);
	if (!ast->pvt->pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	nbs_destroy(p);
	ast->pvt->pvt = NULL;
	ast_setstate(ast, AST_STATE_DOWN);
	return 0;
}

static struct ast_frame  *nbs_xread(struct ast_channel *ast)
{
	struct nbs_pvt *p = ast->pvt->pvt;
	

	/* Some nice norms */
	p->fr.datalen = 0;
	p->fr.samples = 0;
	p->fr.data =  NULL;
	p->fr.src = type;
	p->fr.offset = 0;
	p->fr.mallocd=0;
	p->fr.delivery.tv_sec = 0;
	p->fr.delivery.tv_usec = 0;

	ast_log(LOG_DEBUG, "Returning null frame on %s\n", ast->name);

	return &p->fr;
}

static int nbs_xwrite(struct ast_channel *ast, struct ast_frame *frame)
{
	struct nbs_pvt *p = ast->pvt->pvt;
	/* Write a frame of (presumably voice) data */
	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype != AST_FRAME_IMAGE)
			ast_log(LOG_WARNING, "Don't know what to do with  frame type '%d'\n", frame->frametype);
		return 0;
	}
	if (!(frame->subclass &
		(AST_FORMAT_SLINEAR))) {
		ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		return 0;
	}
	if (ast->_state != AST_STATE_UP) {
		/* Don't try tos end audio on-hook */
		return 0;
	}
	if (nbs_write(p->nbs, frame->data, frame->datalen / 2) < 0) 
		return -1;
	return 0;
}

static struct ast_channel *nbs_new(struct nbs_pvt *i, int state)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc(1);
	if (tmp) {
		snprintf(tmp->name, sizeof(tmp->name), "NBS/%s", i->stream);
		tmp->type = type;
		tmp->fds[0] = nbs_fd(i->nbs);
		tmp->nativeformats = prefformat;
		tmp->pvt->rawreadformat = prefformat;
		tmp->pvt->rawwriteformat = prefformat;
		tmp->writeformat = prefformat;
		tmp->readformat = prefformat;
		ast_setstate(tmp, state);
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->pvt->pvt = i;
		tmp->pvt->call = nbs_call;
		tmp->pvt->hangup = nbs_hangup;
		tmp->pvt->read = nbs_xread;
		tmp->pvt->write = nbs_xwrite;
		strncpy(tmp->context, context, sizeof(tmp->context)-1);
		strncpy(tmp->exten, "s",  sizeof(tmp->exten) - 1);
		tmp->language[0] = '\0';
		i->owner = tmp;
		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
			}
		}
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}


static struct ast_channel *nbs_request(const char *type, int format, void *data)
{
	int oldformat;
	struct nbs_pvt *p;
	struct ast_channel *tmp = NULL;
	
	oldformat = format;
	format &= (AST_FORMAT_SLINEAR);
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}
	p = nbs_alloc(data);
	if (p) {
		tmp = nbs_new(p, AST_STATE_DOWN);
		if (!tmp)
			nbs_destroy(p);
	}
	return tmp;
}

static int __unload_module(void)
{
	/* First, take us out of the channel loop */
	ast_channel_unregister(type);
	return 0;
}

int unload_module(void)
{
	return __unload_module();
}

int load_module()
{
	/* Make sure we can register our Adtranphone channel type */
	if (ast_channel_register(type, tdesc, 
			 AST_FORMAT_SLINEAR, nbs_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		__unload_module();
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

char *description()
{
	return desc;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
