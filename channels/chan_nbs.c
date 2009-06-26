/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Network broadcast sound support channel driver
 * 
 * \author Mark Spencer <markster@digium.com>
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>nbs</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <nbs.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"

static const char tdesc[] = "Network Broadcast Sound Driver";

/* Only linear is allowed */
static int prefformat = AST_FORMAT_SLINEAR;

static char context[AST_MAX_EXTENSION] = "default";
static const char type[] = "NBS";

/* NBS creates private structures on demand */
   
struct nbs_pvt {
	NBS *nbs;
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	char app[16];					/* Our app */
	char stream[80];				/* Our stream */
	struct ast_frame fr;			/* "null" frame */
	struct ast_module_user *u;		/*! for holding a reference to this module */
};

static struct ast_channel *nbs_request(const char *type, int format, const struct ast_channel *requestor, void *data, int *cause);
static int nbs_call(struct ast_channel *ast, char *dest, int timeout);
static int nbs_hangup(struct ast_channel *ast);
static struct ast_frame *nbs_xread(struct ast_channel *ast);
static int nbs_xwrite(struct ast_channel *ast, struct ast_frame *frame);

static const struct ast_channel_tech nbs_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = AST_FORMAT_SLINEAR,
	.requester = nbs_request,
	.call = nbs_call,
	.hangup = nbs_hangup,
	.read = nbs_xread,
	.write = nbs_xwrite,
};

static int nbs_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct nbs_pvt *p;

	p = ast->tech_pvt;

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "nbs_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	ast_debug(1, "Calling %s on %s\n", dest, ast->name);

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
	ast_module_user_remove(p->u);
	ast_free(p);
}

static struct nbs_pvt *nbs_alloc(void *data)
{
	struct nbs_pvt *p;
	int flags = 0;
	char stream[256];
	char *opts;

	ast_copy_string(stream, data, sizeof(stream));
	if ((opts = strchr(stream, ':'))) {
		*opts = '\0';
		opts++;
	} else
		opts = "";
	p = ast_calloc(1, sizeof(*p));
	if (p) {
		if (!ast_strlen_zero(opts)) {
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
		
		ast_copy_string(p->stream, stream, sizeof(p->stream));
		p->nbs = nbs_newstream("asterisk", stream, flags);
		if (!p->nbs) {
			ast_log(LOG_WARNING, "Unable to allocate new NBS stream '%s' with flags %d\n", stream, flags);
			ast_free(p);
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
	p = ast->tech_pvt;
	ast_debug(1, "nbs_hangup(%s)\n", ast->name);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	nbs_destroy(p);
	ast->tech_pvt = NULL;
	ast_setstate(ast, AST_STATE_DOWN);
	return 0;
}

static struct ast_frame  *nbs_xread(struct ast_channel *ast)
{
	struct nbs_pvt *p = ast->tech_pvt;
	

	/* Some nice norms */
	p->fr.datalen = 0;
	p->fr.samples = 0;
	p->fr.data.ptr =  NULL;
	p->fr.src = type;
	p->fr.offset = 0;
	p->fr.mallocd=0;
	p->fr.delivery.tv_sec = 0;
	p->fr.delivery.tv_usec = 0;

	ast_debug(1, "Returning null frame on %s\n", ast->name);

	return &p->fr;
}

static int nbs_xwrite(struct ast_channel *ast, struct ast_frame *frame)
{
	struct nbs_pvt *p = ast->tech_pvt;
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
	if (nbs_write(p->nbs, frame->data.ptr, frame->datalen / 2) < 0) 
		return -1;
	return 0;
}

static struct ast_channel *nbs_new(struct nbs_pvt *i, int state, const char *linkedid)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc(1, state, 0, 0, "", "s", context, linkedid, 0, "NBS/%s", i->stream);
	if (tmp) {
		tmp->tech = &nbs_tech;
		ast_channel_set_fd(tmp, 0, nbs_fd(i->nbs));
		tmp->nativeformats = prefformat;
		tmp->rawreadformat = prefformat;
		tmp->rawwriteformat = prefformat;
		tmp->writeformat = prefformat;
		tmp->readformat = prefformat;
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->tech_pvt = i;
		ast_copy_string(tmp->context, context, sizeof(tmp->context));
		ast_copy_string(tmp->exten, "s",  sizeof(tmp->exten));
		ast_string_field_set(tmp, language, "");
		i->owner = tmp;
		i->u = ast_module_user_add(tmp);
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


static struct ast_channel *nbs_request(const char *type, int format, const struct ast_channel *requestor, void *data, int *cause)
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
		tmp = nbs_new(p, AST_STATE_DOWN, requestor ? requestor->linkedid : NULL);
		if (!tmp)
			nbs_destroy(p);
	}
	return tmp;
}

static int unload_module(void)
{
	/* First, take us out of the channel loop */
	ast_channel_unregister(&nbs_tech);
	return 0;
}

static int load_module(void)
{
	/* Make sure we can register our channel type */
	if (ast_channel_register(&nbs_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Network Broadcast Sound Support");
