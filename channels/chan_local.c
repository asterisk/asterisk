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
 * \author Mark Spencer <markster@digium.com>
 *
 * \brief Local Proxy Channel
 * 
 * \ingroup channel_drivers
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>

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
#include "asterisk/stringfields.h"
#include "asterisk/devicestate.h"

static const char tdesc[] = "Local Proxy Channel Driver";

#define IS_OUTBOUND(a,b) (a == b->chan ? 1 : 0)

static struct ast_channel *local_request(const char *type, int format, void *data, int *cause);
static int local_digit(struct ast_channel *ast, char digit);
static int local_call(struct ast_channel *ast, char *dest, int timeout);
static int local_hangup(struct ast_channel *ast);
static int local_answer(struct ast_channel *ast);
static struct ast_frame *local_read(struct ast_channel *ast);
static int local_write(struct ast_channel *ast, struct ast_frame *f);
static int local_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);
static int local_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int local_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen);
static int local_sendtext(struct ast_channel *ast, const char *text);
static int local_devicestate(void *data);

/* PBX interface structure for channel registration */
static const struct ast_channel_tech local_tech = {
	.type = "Local",
	.description = tdesc,
	.capabilities = -1,
	.requester = local_request,
	.send_digit = local_digit,
	.call = local_call,
	.hangup = local_hangup,
	.answer = local_answer,
	.read = local_read,
	.write = local_write,
	.write_video = local_write,
	.exception = local_read,
	.indicate = local_indicate,
	.fixup = local_fixup,
	.send_html = local_sendhtml,
	.send_text = local_sendtext,
	.devicestate = local_devicestate,
};

struct local_pvt {
	ast_mutex_t lock;			/* Channel private lock */
	char context[AST_MAX_CONTEXT];		/* Context to call */
	char exten[AST_MAX_EXTENSION];		/* Extension to call */
	int reqformat;				/* Requested format */
	int glaredetect;			/* Detect glare on hangup */
	int cancelqueue;			/* Cancel queue */
	int alreadymasqed;			/* Already masqueraded */
	int launchedpbx;			/* Did we launch the PBX */
	int nooptimization;			/* Don't leave masq state */
	struct ast_channel *owner;		/* Master Channel */
	struct ast_channel *chan;		/* Outbound channel */
	struct ast_module_user *u_owner;	/*! reference to keep the module loaded while in use */
	struct ast_module_user *u_chan;		/*! reference to keep the module loaded while in use */
	AST_LIST_ENTRY(local_pvt) list;		/* Next entity */
};

static AST_LIST_HEAD_STATIC(locals, local_pvt);

/*! \brief Adds devicestate to local channels */
static int local_devicestate(void *data)
{
	char *exten;
	char *context;

	int res;
		
	exten = ast_strdupa(data);
	context = strchr(exten, '@');

	if (!context) {
		ast_log(LOG_WARNING, "Someone used Local/%s somewhere without a @context. This is bad.\n", exten);
		return AST_DEVICE_INVALID;	
	}

	*context = '\0';
	context = context + 1;

	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Checking if extension %s@%s exists (devicestate)\n", exten, context);
	res = ast_exists_extension(NULL, context, exten, 1, NULL);
	if (!res) {
		
		return AST_DEVICE_INVALID;
	} else
		return AST_DEVICE_UNKNOWN;
}

static int local_queue_frame(struct local_pvt *p, int isoutbound, struct ast_frame *f, struct ast_channel *us)
{
	struct ast_channel *other;
retrylock:		
	/* Recalculate outbound channel */
	if (isoutbound) {
		other = p->owner;
	} else {
		other = p->chan;
	}
	/* Set glare detection */
	p->glaredetect = 1;
	if (p->cancelqueue) {
		/* We had a glare on the hangup.  Forget all this business,
		return and destroy p.  */
		ast_mutex_unlock(&p->lock);
		ast_mutex_destroy(&p->lock);
		free(p);
		return -1;
	}
	if (!other) {
		p->glaredetect = 0;
		return 0;
	}
	if (ast_mutex_trylock(&other->lock)) {
		/* Failed to lock.  Release main lock and try again */
		ast_mutex_unlock(&p->lock);
		if (us) {
			if (ast_mutex_unlock(&us->lock)) {
				ast_log(LOG_WARNING, "%s wasn't locked while sending %d/%d\n",
					us->name, f->frametype, f->subclass);
				us = NULL;
			}
		}
		/* Wait just a bit */
		usleep(1);
		/* Only we can destroy ourselves, so we can't disappear here */
		if (us)
			ast_mutex_lock(&us->lock);
		ast_mutex_lock(&p->lock);
		goto retrylock;
	}
	ast_queue_frame(other, f);
	ast_mutex_unlock(&other->lock);
	p->glaredetect = 0;
	return 0;
}

static int local_answer(struct ast_channel *ast)
{
	struct local_pvt *p = ast->tech_pvt;
	int isoutbound;
	int res = -1;

	ast_mutex_lock(&p->lock);
	isoutbound = IS_OUTBOUND(ast, p);
	if (isoutbound) {
		/* Pass along answer since somebody answered us */
		struct ast_frame answer = { AST_FRAME_CONTROL, AST_CONTROL_ANSWER };
		res = local_queue_frame(p, isoutbound, &answer, ast);
	} else
		ast_log(LOG_WARNING, "Huh?  Local is being asked to answer?\n");
	ast_mutex_unlock(&p->lock);
	return res;
}

static void check_bridge(struct local_pvt *p, int isoutbound)
{
	if (p->alreadymasqed || p->nooptimization)
		return;
	if (!p->chan || !p->owner)
		return;

	/* only do the masquerade if we are being called on the outbound channel,
	   if it has been bridged to another channel and if there are no pending
	   frames on the owner channel (because they would be transferred to the
	   outbound channel during the masquerade)
	*/
	if (isoutbound && p->chan->_bridge /* Not ast_bridged_channel!  Only go one step! */ && AST_LIST_EMPTY(&p->owner->readq)) {
		/* Masquerade bridged channel into owner */
		/* Lock everything we need, one by one, and give up if
		   we can't get everything.  Remember, we'll get another
		   chance in just a little bit */
		if (!ast_mutex_trylock(&(p->chan->_bridge)->lock)) {
			if (!p->chan->_bridge->_softhangup) {
				if (!ast_mutex_trylock(&p->owner->lock)) {
					if (!p->owner->_softhangup) {
						ast_channel_masquerade(p->owner, p->chan->_bridge);
						p->alreadymasqed = 1;
					}
					ast_mutex_unlock(&p->owner->lock);
				}
				ast_mutex_unlock(&(p->chan->_bridge)->lock);
			}
		}
	/* We only allow masquerading in one 'direction'... it's important to preserve the state
	   (group variables, etc.) that live on p->chan->_bridge (and were put there by the dialplan)
	   when the local channels go away.
	*/
#if 0
	} else if (!isoutbound && p->owner && p->owner->_bridge && p->chan && AST_LIST_EMPTY(&p->chan->readq)) {
		/* Masquerade bridged channel into chan */
		if (!ast_mutex_trylock(&(p->owner->_bridge)->lock)) {
			if (!p->owner->_bridge->_softhangup) {
				if (!ast_mutex_trylock(&p->chan->lock)) {
					if (!p->chan->_softhangup) {
						ast_channel_masquerade(p->chan, p->owner->_bridge);
						p->alreadymasqed = 1;
					}
					ast_mutex_unlock(&p->chan->lock);
				}
			}
			ast_mutex_unlock(&(p->owner->_bridge)->lock);
		}
#endif
	}
}

static struct ast_frame  *local_read(struct ast_channel *ast)
{
	return &ast_null_frame;
}

static int local_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct local_pvt *p = ast->tech_pvt;
	int res = -1;
	int isoutbound;

	/* Just queue for delivery to the other side */
	ast_mutex_lock(&p->lock);
	isoutbound = IS_OUTBOUND(ast, p);
	if (f && (f->frametype == AST_FRAME_VOICE || f->frametype == AST_FRAME_VIDEO))
		check_bridge(p, isoutbound);
	if (!p->alreadymasqed)
		res = local_queue_frame(p, isoutbound, f, ast);
	else {
		if (option_debug)
			ast_log(LOG_DEBUG, "Not posting to queue since already masked on '%s'\n", ast->name);
		res = 0;
	}
	ast_mutex_unlock(&p->lock);
	return res;
}

static int local_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct local_pvt *p = newchan->tech_pvt;
	ast_mutex_lock(&p->lock);

	if ((p->owner != oldchan) && (p->chan != oldchan)) {
		ast_log(LOG_WARNING, "Old channel wasn't %p but was %p/%p\n", oldchan, p->owner, p->chan);
		ast_mutex_unlock(&p->lock);
		return -1;
	}
	if (p->owner == oldchan)
		p->owner = newchan;
	else
		p->chan = newchan;
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int local_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen)
{
	struct local_pvt *p = ast->tech_pvt;
	int res = -1;
	struct ast_frame f = { AST_FRAME_CONTROL, };
	int isoutbound;

	/* Queue up a frame representing the indication as a control frame */
	ast_mutex_lock(&p->lock);
	isoutbound = IS_OUTBOUND(ast, p);
	f.subclass = condition;
	res = local_queue_frame(p, isoutbound, &f, ast);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int local_digit(struct ast_channel *ast, char digit)
{
	struct local_pvt *p = ast->tech_pvt;
	int res = -1;
	struct ast_frame f = { AST_FRAME_DTMF, };
	int isoutbound;

	ast_mutex_lock(&p->lock);
	isoutbound = IS_OUTBOUND(ast, p);
	f.subclass = digit;
	res = local_queue_frame(p, isoutbound, &f, ast);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int local_sendtext(struct ast_channel *ast, const char *text)
{
	struct local_pvt *p = ast->tech_pvt;
	int res = -1;
	struct ast_frame f = { AST_FRAME_TEXT, };
	int isoutbound;

	ast_mutex_lock(&p->lock);
	isoutbound = IS_OUTBOUND(ast, p);
	f.data = (char *) text;
	f.datalen = strlen(text) + 1;
	res = local_queue_frame(p, isoutbound, &f, ast);
	ast_mutex_unlock(&p->lock);
	return res;
}

static int local_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen)
{
	struct local_pvt *p = ast->tech_pvt;
	int res = -1;
	struct ast_frame f = { AST_FRAME_HTML, };
	int isoutbound;

	ast_mutex_lock(&p->lock);
	isoutbound = IS_OUTBOUND(ast, p);
	f.subclass = subclass;
	f.data = (char *)data;
	f.datalen = datalen;
	res = local_queue_frame(p, isoutbound, &f, ast);
	ast_mutex_unlock(&p->lock);
	return res;
}

/*! \brief Initiate new call, part of PBX interface 
 * 	dest is the dial string */
static int local_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct local_pvt *p = ast->tech_pvt;
	int res;
	struct ast_var_t *varptr = NULL, *new;
	size_t len, namelen;
	
	ast_mutex_lock(&p->lock);

	p->chan->cid.cid_num = ast_strdup(p->owner->cid.cid_num);
	p->chan->cid.cid_name = ast_strdup(p->owner->cid.cid_name);
	p->chan->cid.cid_rdnis = ast_strdup(p->owner->cid.cid_rdnis);
	p->chan->cid.cid_ani = ast_strdup(p->owner->cid.cid_ani);
	p->chan->cid.cid_pres = p->owner->cid.cid_pres;
	ast_string_field_set(p->chan, language, p->owner->language);
	ast_string_field_set(p->chan, accountcode, p->owner->accountcode);
	p->chan->cdrflags = p->owner->cdrflags;

	/* copy the channel variables from the incoming channel to the outgoing channel */
	/* Note that due to certain assumptions, they MUST be in the same order */
	AST_LIST_TRAVERSE(&p->owner->varshead, varptr, entries) {
		namelen = strlen(varptr->name);
		len = sizeof(struct ast_var_t) + namelen + strlen(varptr->value) + 2;
		if ((new = ast_calloc(1, len))) {
			memcpy(new, varptr, len);
			new->value = &(new->name[0]) + namelen + 1;
			AST_LIST_INSERT_TAIL(&p->chan->varshead, new, entries);
		}
	}

	p->launchedpbx = 1;

	/* Start switch on sub channel */
	res = ast_pbx_start(p->chan);
	ast_mutex_unlock(&p->lock);
	return res;
}

#if 0
static void local_destroy(struct local_pvt *p)
{
	struct local_pvt *cur;

	AST_LIST_LOCK(&locals);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&locals, cur, list) {
		if (cur == p) {
			AST_LIST_REMOVE_CURRENT(&locals, list);
			ast_mutex_destroy(&cur->lock);
			free(cur);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	AST_LIST_UNLOCK(&locals);
	if (!cur)
		ast_log(LOG_WARNING, "Unable ot find local '%s@%s' in local list\n", p->exten, p->context);
}
#endif

/*! \brief Hangup a call through the local proxy channel */
static int local_hangup(struct ast_channel *ast)
{
	struct local_pvt *p = ast->tech_pvt;
	int isoutbound;
	struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_HANGUP };
	struct ast_channel *ochan = NULL;
	int glaredetect;

	ast_mutex_lock(&p->lock);
	isoutbound = IS_OUTBOUND(ast, p);
	if (isoutbound) {
		const char *status = pbx_builtin_getvar_helper(p->chan, "DIALSTATUS");
		if ((status) && (p->owner))
			pbx_builtin_setvar_helper(p->owner, "CHANLOCALSTATUS", status);
		p->chan = NULL;
		p->launchedpbx = 0;
		ast_module_user_remove(p->u_chan);
	} else {
		p->owner = NULL;
		ast_module_user_remove(p->u_owner);
	}
	
	ast->tech_pvt = NULL;
	
	if (!p->owner && !p->chan) {
		/* Okay, done with the private part now, too. */
		glaredetect = p->glaredetect;
		/* If we have a queue holding, don't actually destroy p yet, but
		   let local_queue do it. */
		if (p->glaredetect)
			p->cancelqueue = 1;
		ast_mutex_unlock(&p->lock);
		/* Remove from list */
		AST_LIST_LOCK(&locals);
		AST_LIST_REMOVE(&locals, p, list);
		AST_LIST_UNLOCK(&locals);
		/* Grab / release lock just in case */
		ast_mutex_lock(&p->lock);
		ast_mutex_unlock(&p->lock);
		/* And destroy */
		if (!glaredetect) {
			ast_mutex_destroy(&p->lock);
			free(p);
		}
		return 0;
	}
	if (p->chan && !p->launchedpbx)
		/* Need to actually hangup since there is no PBX */
		ochan = p->chan;
	else
		local_queue_frame(p, isoutbound, &f, NULL);
	ast_mutex_unlock(&p->lock);
	if (ochan)
		ast_hangup(ochan);
	return 0;
}

/*! \brief Create a call structure */
static struct local_pvt *local_alloc(const char *data, int format)
{
	struct local_pvt *tmp;
	char *c;
	char *opts;

	if (!(tmp = ast_calloc(1, sizeof(*tmp))))
		return NULL;
	
	ast_mutex_init(&tmp->lock);
	ast_copy_string(tmp->exten, data, sizeof(tmp->exten));
	opts = strchr(tmp->exten, '/');
	if (opts) {
		*opts++ = '\0';
		if (strchr(opts, 'n'))
			tmp->nooptimization = 1;
	}
	c = strchr(tmp->exten, '@');
	if (c)
		*c++ = '\0';
	ast_copy_string(tmp->context, c ? c : "default", sizeof(tmp->context));
	tmp->reqformat = format;
	if (!ast_exists_extension(NULL, tmp->context, tmp->exten, 1, NULL)) {
		ast_log(LOG_NOTICE, "No such extension/context %s@%s creating local channel\n", tmp->exten, tmp->context);
		ast_mutex_destroy(&tmp->lock);
		free(tmp);
		tmp = NULL;
	} else {
		/* Add to list */
		AST_LIST_LOCK(&locals);
		AST_LIST_INSERT_HEAD(&locals, tmp, list);
		AST_LIST_UNLOCK(&locals);
	}
	
	return tmp;
}

/*! \brief Start new local channel */
static struct ast_channel *local_new(struct local_pvt *p, int state)
{
	struct ast_channel *tmp, *tmp2;
	int randnum = ast_random() & 0xffff;

	tmp = ast_channel_alloc(1);
	tmp2 = ast_channel_alloc(1);
	if (!tmp || !tmp2) {
		if (tmp)
			ast_channel_free(tmp);
		if (tmp2)
			ast_channel_free(tmp2);
		ast_log(LOG_WARNING, "Unable to allocate channel structure(s)\n");
		return NULL;
	} 

	tmp2->tech = tmp->tech = &local_tech;
	tmp->nativeformats = p->reqformat;
	tmp2->nativeformats = p->reqformat;
	ast_string_field_build(tmp, name, "Local/%s@%s-%04x,1", p->exten, p->context, randnum);
	ast_string_field_build(tmp2, name, "Local/%s@%s-%04x,2", p->exten, p->context, randnum);
	ast_setstate(tmp, state);
	ast_setstate(tmp2, AST_STATE_RING);
	tmp->writeformat = p->reqformat;
	tmp2->writeformat = p->reqformat;
	tmp->rawwriteformat = p->reqformat;
	tmp2->rawwriteformat = p->reqformat;
	tmp->readformat = p->reqformat;
	tmp2->readformat = p->reqformat;
	tmp->rawreadformat = p->reqformat;
	tmp2->rawreadformat = p->reqformat;
	tmp->tech_pvt = p;
	tmp2->tech_pvt = p;
	p->owner = tmp;
	p->chan = tmp2;
	p->u_owner = ast_module_user_add(p->owner);
	p->u_chan = ast_module_user_add(p->chan);
	ast_copy_string(tmp->context, p->context, sizeof(tmp->context));
	ast_copy_string(tmp2->context, p->context, sizeof(tmp2->context));
	ast_copy_string(tmp2->exten, p->exten, sizeof(tmp->exten));
	tmp->priority = 1;
	tmp2->priority = 1;

	return tmp;
}


/*! \brief Part of PBX interface */
static struct ast_channel *local_request(const char *type, int format, void *data, int *cause)
{
	struct local_pvt *p;
	struct ast_channel *chan = NULL;

	p = local_alloc(data, format);
	if (p)
		chan = local_new(p, AST_STATE_DOWN);
	return chan;
}

/*! \brief CLI command "local show channels" */
static int locals_show(int fd, int argc, char **argv)
{
	struct local_pvt *p;

	if (argc != 3)
		return RESULT_SHOWUSAGE;
	if (AST_LIST_EMPTY(&locals))
		ast_cli(fd, "No local channels in use\n");
	AST_LIST_LOCK(&locals);
	AST_LIST_TRAVERSE(&locals, p, list) {
		ast_mutex_lock(&p->lock);
		ast_cli(fd, "%s -- %s@%s\n", p->owner ? p->owner->name : "<unowned>", p->exten, p->context);
		ast_mutex_unlock(&p->lock);
	}
	AST_LIST_UNLOCK(&locals);
	return RESULT_SUCCESS;
}

static char show_locals_usage[] = 
"Usage: local show channels\n"
"       Provides summary information on active local proxy channels.\n";

static struct ast_cli_entry cli_show_locals = {
	{ "local", "show", "channels", NULL }, locals_show, 
	"Show status of local channels", show_locals_usage, NULL };

/*! \brief Load module into PBX, register channel */
static int load_module(void)
{
	/* Make sure we can register our channel type */
	if (ast_channel_register(&local_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'Local'\n");
		return -1;
	}
	ast_cli_register(&cli_show_locals);
	return 0;
}

/*! \brief Unload the local proxy channel from Asterisk */
static int unload_module(void)
{
	struct local_pvt *p;

	/* First, take us out of the channel loop */
	ast_cli_unregister(&cli_show_locals);
	ast_channel_unregister(&local_tech);
	if (!AST_LIST_LOCK(&locals)) {
		/* Hangup all interfaces if they have an owner */
		AST_LIST_TRAVERSE(&locals, p, list) {
			if (p->owner)
				ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
		}
		AST_LIST_UNLOCK(&locals);
		AST_LIST_HEAD_DESTROY(&locals);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}		
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Local Proxy Channel");
