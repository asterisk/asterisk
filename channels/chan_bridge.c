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

static struct ast_channel *bridge_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, void *data, int *cause);
static int bridge_call(struct ast_channel *ast, char *dest, int timeout);
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
	ast_mutex_t lock;           /*!< Lock that protects this structure */
	struct ast_channel *input;  /*!< Input channel - talking to source */
	struct ast_channel *output; /*!< Output channel - talking to bridge */
};

/*! \brief Called when the user of this channel wants to get the actual channel in the bridge */
static struct ast_channel *bridge_bridgedchannel(struct ast_channel *chan, struct ast_channel *bridge)
{
	struct bridge_pvt *p = chan->tech_pvt;
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
	struct bridge_pvt *p = ast->tech_pvt;
	struct ast_channel *other;

	ast_mutex_lock(&p->lock);

	other = (p->input == ast ? p->output : p->input);

	while (other && ast_channel_trylock(other)) {
		ast_mutex_unlock(&p->lock);
		do {
			CHANNEL_DEADLOCK_AVOIDANCE(ast);
		} while (ast_mutex_trylock(&p->lock));
		other = (p->input == ast ? p->output : p->input);
	}

	/* We basically queue the frame up on the other channel if present */
	if (other) {
		ast_queue_frame(other, f);
		ast_channel_unlock(other);
	}

	ast_mutex_unlock(&p->lock);

	return 0;
}

/*! \brief Called when the channel should actually be dialed */
static int bridge_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct bridge_pvt *p = ast->tech_pvt;

	/* If no bridge has been provided on the input channel, bail out */
	if (!ast->bridge) {
		return -1;
	}

	/* Impart the output channel upon the given bridge of the input channel */
	ast_bridge_impart(p->input->bridge, p->output, NULL, NULL);

	return 0;
}

/*! \brief Helper function to not deadlock when queueing the hangup frame */
static void bridge_queue_hangup(struct bridge_pvt *p, struct ast_channel *us)
{
	struct ast_channel *other = (p->input == us ? p->output : p->input);

	while (other && ast_channel_trylock(other)) {
		ast_mutex_unlock(&p->lock);
		do {
			CHANNEL_DEADLOCK_AVOIDANCE(us);
		} while (ast_mutex_trylock(&p->lock));
		other = (p->input == us ? p->output : p->input);
	}

	/* We basically queue the frame up on the other channel if present */
	if (other) {
		ast_queue_hangup(other);
		ast_channel_unlock(other);
	}

	return;
}

/*! \brief Called when a channel should be hung up */
static int bridge_hangup(struct ast_channel *ast)
{
	struct bridge_pvt *p = ast->tech_pvt;

	ast_mutex_lock(&p->lock);

	/* Figure out which channel this is... and set it to NULL as it has gone, but also queue up a hangup frame. */
	if (p->input == ast) {
		if (p->output) {
			bridge_queue_hangup(p, ast);
		}
		p->input = NULL;
	} else if (p->output == ast) {
		if (p->input) {
			bridge_queue_hangup(p, ast);
		}
		p->output = NULL;
	}

	/* Deal with the Asterisk portion of it */
	ast->tech_pvt = NULL;

	/* If both sides have been terminated free the structure and be done with things */
	if (!p->input && !p->output) {
		ast_mutex_unlock(&p->lock);
		ast_mutex_destroy(&p->lock);
		ast_free(p);
	} else {
		ast_mutex_unlock(&p->lock);
	}

	return 0;
}

/*! \brief Called when we want to place a call somewhere, but not actually call it... yet */
static struct ast_channel *bridge_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, void *data, int *cause)
{
	struct bridge_pvt *p = NULL;
	struct ast_format slin;

	/* Try to allocate memory for our very minimal pvt structure */
	if (!(p = ast_calloc(1, sizeof(*p)))) {
		return NULL;
	}

	/* Try to grab two Asterisk channels to use as input and output channels */
	if (!(p->input = ast_channel_alloc(1, AST_STATE_UP, 0, 0, "", "", "", requestor ? requestor->linkedid : NULL, 0, "Bridge/%p-input", p))) {
		ast_free(p);
		return NULL;
	}
	if (!(p->output = ast_channel_alloc(1, AST_STATE_UP, 0, 0, "", "", "", requestor ? requestor->linkedid : NULL, 0, "Bridge/%p-output", p))) {
		p->input = ast_channel_release(p->input);
		ast_free(p);
		return NULL;
	}

	/* Setup the lock on the pvt structure, we will need that */
	ast_mutex_init(&p->lock);

	/* Setup parameters on both new channels */
	p->input->tech = p->output->tech = &bridge_tech;
	p->input->tech_pvt = p->output->tech_pvt = p;

	ast_format_set(&slin, AST_FORMAT_SLINEAR, 0);

	ast_format_cap_add(p->input->nativeformats, &slin);
	ast_format_cap_add(p->output->nativeformats, &slin);
	ast_format_copy(&p->input->readformat, &slin);
	ast_format_copy(&p->output->readformat, &slin);
	ast_format_copy(&p->input->rawreadformat, &slin);
	ast_format_copy(&p->output->rawreadformat, &slin);
	ast_format_copy(&p->input->writeformat, &slin);
	ast_format_copy(&p->output->writeformat, &slin);
	ast_format_copy(&p->input->rawwriteformat, &slin);
	ast_format_copy(&p->output->rawwriteformat, &slin);

	ast_answer(p->output);
	ast_answer(p->input);

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
