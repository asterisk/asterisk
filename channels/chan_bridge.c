/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \author Joshua Colp <jcolp@digium.com>
 *
 * \brief Bridge Interaction Channel
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <fcntl.h>
#include <sys/signal.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/bridging.h"
#include "asterisk/astobj2.h"

static struct ast_channel *bridge_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause);
static int bridge_call(struct ast_channel *ast, const char *dest, int timeout);
static int bridge_hangup(struct ast_channel *ast);
static struct ast_frame *bridge_read(struct ast_channel *ast);
static int bridge_write(struct ast_channel *ast, struct ast_frame *f);
static struct ast_channel *bridge_bridgedchannel(struct ast_channel *chan, struct ast_channel *bridge);

static struct ast_channel_tech bridge_tech = {
	.type = "Bridge",
	.description = "Bridge Interaction Channel",
	.requester = bridge_request,
	.call = bridge_call,
	.hangup = bridge_hangup,
	.read = bridge_read,
	.write = bridge_write,
	.write_video = bridge_write,
	.exception = bridge_read,
	.bridged_channel = bridge_bridgedchannel,
};

struct bridge_pvt {
	struct ast_channel *input;  /*!< Input channel - talking to source */
	struct ast_channel *output; /*!< Output channel - talking to bridge */
};

/*! \brief Called when the user of this channel wants to get the actual channel in the bridge */
static struct ast_channel *bridge_bridgedchannel(struct ast_channel *chan, struct ast_channel *bridge)
{
	struct bridge_pvt *p = ast_channel_tech_pvt(chan);
	return (chan == p->input) ? p->output : bridge;
}

/*! \brief Called when a frame should be read from the channel */
static struct ast_frame  *bridge_read(struct ast_channel *ast)
{
	return &ast_null_frame;
}

/*! \brief Called when a frame should be written out to a channel */
static int bridge_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct bridge_pvt *p = ast_channel_tech_pvt(ast);
	struct ast_channel *other = NULL;

	ao2_lock(p);
	/* only write frames to output. */
	if (p->input == ast) {
		other = p->output;
		if (other) {
			ast_channel_ref(other);
		}
	}
	ao2_unlock(p);

	if (other) {
		ast_channel_unlock(ast);
		ast_queue_frame(other, f);
		ast_channel_lock(ast);
		other = ast_channel_unref(other);
	}

	return 0;
}

/*! \brief Called when the channel should actually be dialed */
static int bridge_call(struct ast_channel *ast, const char *dest, int timeout)
{
	struct bridge_pvt *p = ast_channel_tech_pvt(ast);

	/* If no bridge has been provided on the input channel, bail out */
	if (!ast_channel_internal_bridge(ast)) {
		return -1;
	}

	/* Impart the output channel upon the given bridge of the input channel */
	ast_bridge_impart(ast_channel_internal_bridge(p->input), p->output, NULL, NULL, 0);

	return 0;
}

/*! \brief Called when a channel should be hung up */
static int bridge_hangup(struct ast_channel *ast)
{
	struct bridge_pvt *p = ast_channel_tech_pvt(ast);

	if (!p) {
		return 0;
	}

	ao2_lock(p);
	if (p->input == ast) {
		p->input = NULL;
	} else if (p->output == ast) {
		p->output = NULL;
	}
	ao2_unlock(p);

	ast_channel_tech_pvt_set(ast, NULL);
	ao2_ref(p, -1);

	return 0;
}

/*! \brief Called when we want to place a call somewhere, but not actually call it... yet */
static struct ast_channel *bridge_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct bridge_pvt *p = NULL;
	struct ast_format slin;

	/* Try to allocate memory for our very minimal pvt structure */
	if (!(p = ao2_alloc(sizeof(*p), NULL))) {
		return NULL;
	}

	/* Try to grab two Asterisk channels to use as input and output channels */
	if (!(p->input = ast_channel_alloc(1, AST_STATE_UP, 0, 0, "", "", "", requestor ? ast_channel_linkedid(requestor) : NULL, 0, "Bridge/%p-input", p))) {
		ao2_ref(p, -1);
		return NULL;
	}
	if (!(p->output = ast_channel_alloc(1, AST_STATE_UP, 0, 0, "", "", "", requestor ? ast_channel_linkedid(requestor) : NULL, 0, "Bridge/%p-output", p))) {
		p->input = ast_channel_release(p->input);
		ao2_ref(p, -1);
		return NULL;
	}

	/* Setup parameters on both new channels */
	ast_channel_tech_set(p->input, &bridge_tech);
	ast_channel_tech_set(p->output, &bridge_tech);

	ao2_ref(p, 2);
	ast_channel_tech_pvt_set(p->input, p);
	ast_channel_tech_pvt_set(p->output, p);

	ast_format_set(&slin, AST_FORMAT_SLINEAR, 0);

	ast_format_cap_add(ast_channel_nativeformats(p->input), &slin);
	ast_format_cap_add(ast_channel_nativeformats(p->output), &slin);
	ast_format_copy(ast_channel_readformat(p->input), &slin);
	ast_format_copy(ast_channel_readformat(p->output), &slin);
	ast_format_copy(ast_channel_rawreadformat(p->input), &slin);
	ast_format_copy(ast_channel_rawreadformat(p->output), &slin);
	ast_format_copy(ast_channel_writeformat(p->input), &slin);
	ast_format_copy(ast_channel_writeformat(p->output), &slin);
	ast_format_copy(ast_channel_rawwriteformat(p->input), &slin);
	ast_format_copy(ast_channel_rawwriteformat(p->output), &slin);

	ast_answer(p->output);
	ast_answer(p->input);

	/* remove the reference from the alloc. The channels now own the pvt. */
	ao2_ref(p, -1);
	return p->input;
}

/*! \brief Load module into PBX, register channel */
static int load_module(void)
{
	if (!(bridge_tech.capabilities = ast_format_cap_alloc())) {
		return AST_MODULE_LOAD_FAILURE;
	}

	ast_format_cap_add_all(bridge_tech.capabilities);
	/* Make sure we can register our channel type */
	if (ast_channel_register(&bridge_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'Bridge'\n");
		return AST_MODULE_LOAD_FAILURE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

/*! \brief Unload the bridge interaction channel from Asterisk */
static int unload_module(void)
{
	ast_channel_unregister(&bridge_tech);
	bridge_tech.capabilities = ast_format_cap_destroy(bridge_tech.capabilities);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Bridge Interaction Channel",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
);
