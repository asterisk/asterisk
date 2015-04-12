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
	<support_level>extended</support_level>	
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

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
#include "asterisk/format_cache.h"

static const char tdesc[] = "Network Broadcast Sound Driver";

static char context[AST_MAX_EXTENSION] = "default";
static const char type[] = "NBS";

/* NBS creates private structures on demand */
   
struct nbs_pvt {
	NBS *nbs;
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	char app[16];					/* Our app */
	char stream[80];				/* Our stream */
	struct ast_module_user *u;		/*! for holding a reference to this module */
};

static struct ast_channel *nbs_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int nbs_call(struct ast_channel *ast, const char *dest, int timeout);
static int nbs_hangup(struct ast_channel *ast);
static struct ast_frame *nbs_xread(struct ast_channel *ast);
static int nbs_xwrite(struct ast_channel *ast, struct ast_frame *frame);

static struct ast_channel_tech nbs_tech = {
	.type = type,
	.description = tdesc,
	.requester = nbs_request,
	.call = nbs_call,
	.hangup = nbs_hangup,
	.read = nbs_xread,
	.write = nbs_xwrite,
};

static int nbs_call(struct ast_channel *ast, const char *dest, int timeout)
{
	struct nbs_pvt *p;

	p = ast_channel_tech_pvt(ast);

	if ((ast_channel_state(ast) != AST_STATE_DOWN) && (ast_channel_state(ast) != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "nbs_call called on %s, neither down nor reserved\n", ast_channel_name(ast));
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	ast_debug(1, "Calling %s on %s\n", dest, ast_channel_name(ast));

	/* If we can't connect, return congestion */
	if (nbs_connect(p->nbs)) {
		ast_log(LOG_WARNING, "NBS Connection failed on %s\n", ast_channel_name(ast));
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

static struct nbs_pvt *nbs_alloc(const char *data)
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
	p = ast_channel_tech_pvt(ast);
	ast_debug(1, "nbs_hangup(%s)\n", ast_channel_name(ast));
	if (!ast_channel_tech_pvt(ast)) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	nbs_destroy(p);
	ast_channel_tech_pvt_set(ast, NULL);
	ast_setstate(ast, AST_STATE_DOWN);
	return 0;
}

static struct ast_frame  *nbs_xread(struct ast_channel *ast)
{
	ast_debug(1, "Returning null frame on %s\n", ast_channel_name(ast));

	return &ast_null_frame;
}

static int nbs_xwrite(struct ast_channel *ast, struct ast_frame *frame)
{
	struct nbs_pvt *p = ast_channel_tech_pvt(ast);
	if (ast_channel_state(ast) != AST_STATE_UP) {
		/* Don't try tos end audio on-hook */
		return 0;
	}
	if (nbs_write(p->nbs, frame->data.ptr, frame->datalen / 2) < 0) 
		return -1;
	return 0;
}

static struct ast_channel *nbs_new(struct nbs_pvt *i, int state, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc(1, state, 0, 0, "", "s", context, assignedids, requestor, 0, "NBS/%s", i->stream);
	if (tmp) {
		ast_channel_tech_set(tmp, &nbs_tech);
		ast_channel_set_fd(tmp, 0, nbs_fd(i->nbs));

		ast_channel_nativeformats_set(tmp, nbs_tech.capabilities);
		ast_channel_set_rawreadformat(tmp, ast_format_slin);
		ast_channel_set_rawwriteformat(tmp, ast_format_slin);
		ast_channel_set_writeformat(tmp, ast_format_slin);
		ast_channel_set_readformat(tmp, ast_format_slin);
		if (state == AST_STATE_RING)
			ast_channel_rings_set(tmp, 1);
		ast_channel_tech_pvt_set(tmp, i);
		ast_channel_context_set(tmp, context);
		ast_channel_exten_set(tmp, "s");
		ast_channel_language_set(tmp, "");
		i->owner = tmp;
		i->u = ast_module_user_add(tmp);
		ast_channel_unlock(tmp);
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(tmp));
				ast_hangup(tmp);
			}
		}
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}


static struct ast_channel *nbs_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct nbs_pvt *p;
	struct ast_channel *tmp = NULL;

	if (ast_format_cap_iscompatible_format(cap, ast_format_slin) == AST_FORMAT_CMP_NOT_EQUAL) {
		struct ast_str *cap_buf = ast_str_alloca(64);

		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%s'\n",
			ast_format_cap_get_names(cap, &cap_buf));
		return NULL;
	}
	p = nbs_alloc(data);
	if (p) {
		tmp = nbs_new(p, AST_STATE_DOWN, assignedids, requestor);
		if (!tmp)
			nbs_destroy(p);
	}
	return tmp;
}

static int unload_module(void)
{
	/* First, take us out of the channel loop */
	ast_channel_unregister(&nbs_tech);
	ao2_ref(nbs_tech.capabilities, -1);
	nbs_tech.capabilities = NULL;
	return 0;
}

static int load_module(void)
{
	if (!(nbs_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_format_cap_append(nbs_tech.capabilities, ast_format_slin, 0);
	/* Make sure we can register our channel type */
	if (ast_channel_register(&nbs_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Network Broadcast Sound Support");

