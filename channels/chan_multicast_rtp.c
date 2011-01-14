/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
 * Andreas 'MacBrody' Brodmann <andreas.brodmann@gmail.com>
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
 * \author Andreas 'MacBrody' Broadmann <andreas.brodmann@gmail.com>
 *
 * \brief Multicast RTP Paging Channel
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
#include "asterisk/rtp_engine.h"
#include "asterisk/causes.h"

static const char tdesc[] = "Multicast RTP Paging Channel Driver";

/* Forward declarations */
static struct ast_channel *multicast_rtp_request(const char *type, format_t format, const struct ast_channel *requestor, void *data, int *cause);
static int multicast_rtp_call(struct ast_channel *ast, char *dest, int timeout);
static int multicast_rtp_hangup(struct ast_channel *ast);
static struct ast_frame *multicast_rtp_read(struct ast_channel *ast);
static int multicast_rtp_write(struct ast_channel *ast, struct ast_frame *f);

/* Channel driver declaration */
static const struct ast_channel_tech multicast_rtp_tech = {
	.type = "MulticastRTP",
	.description = tdesc,
	.capabilities = -1,
	.requester = multicast_rtp_request,
	.call = multicast_rtp_call,
	.hangup = multicast_rtp_hangup,
	.read = multicast_rtp_read,
	.write = multicast_rtp_write,
};

/*! \brief Function called when we should read a frame from the channel */
static struct ast_frame  *multicast_rtp_read(struct ast_channel *ast)
{
	return &ast_null_frame;
}

/*! \brief Function called when we should write a frame to the channel */
static int multicast_rtp_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct ast_rtp_instance *instance = ast->tech_pvt;

	return ast_rtp_instance_write(instance, f);
}

/*! \brief Function called when we should actually call the destination */
static int multicast_rtp_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct ast_rtp_instance *instance = ast->tech_pvt;

	ast_queue_control(ast, AST_CONTROL_ANSWER);

	return ast_rtp_instance_activate(instance);
}

/*! \brief Function called when we should hang the channel up */
static int multicast_rtp_hangup(struct ast_channel *ast)
{
	struct ast_rtp_instance *instance = ast->tech_pvt;

	ast_rtp_instance_destroy(instance);

	ast->tech_pvt = NULL;

	return 0;
}

/*! \brief Function called when we should prepare to call the destination */
static struct ast_channel *multicast_rtp_request(const char *type, format_t format, const struct ast_channel *requestor, void *data, int *cause)
{
	char *tmp = ast_strdupa(data), *multicast_type = tmp, *destination, *control;
	struct ast_rtp_instance *instance;
	struct ast_sockaddr control_address = { .len = 0 };
	struct ast_sockaddr destination_address;
	struct ast_channel *chan;
	format_t fmt = ast_best_codec(format);

	/* If no type was given we can't do anything */
	if (ast_strlen_zero(multicast_type)) {
		goto failure;
	}

	if (!(destination = strchr(tmp, '/'))) {
		goto failure;
	}
	*destination++ = '\0';

	if ((control = strchr(destination, '/'))) {
		*control++ = '\0';
		if (!ast_sockaddr_parse(&control_address, control,
					PARSE_PORT_REQUIRE)) {
			goto failure;
		}
	}

	if (!ast_sockaddr_parse(&destination_address, destination,
				PARSE_PORT_REQUIRE)) {
		goto failure;
	}

	if (!(instance = ast_rtp_instance_new("multicast", NULL, &control_address, multicast_type))) {
		goto failure;
	}

	if (!(chan = ast_channel_alloc(1, AST_STATE_DOWN, "", "", "", "", "", requestor ? requestor->linkedid : "", 0, "MulticastRTP/%p", instance))) {
		ast_rtp_instance_destroy(instance);
		goto failure;
	}

	ast_rtp_instance_set_remote_address(instance, &destination_address);

	chan->tech = &multicast_rtp_tech;
	chan->nativeformats = fmt;
	chan->writeformat = fmt;
	chan->readformat = fmt;
	chan->rawwriteformat = fmt;
	chan->rawreadformat = fmt;
	chan->tech_pvt = instance;

	return chan;

failure:
	*cause = AST_CAUSE_FAILURE;
	return NULL;
}

/*! \brief Function called when our module is loaded */
static int load_module(void)
{
	if (ast_channel_register(&multicast_rtp_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'MulticastRTP'\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

/*! \brief Function called when our module is unloaded */
static int unload_module(void)
{
	ast_channel_unregister(&multicast_rtp_tech);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Multicast RTP Paging Channel",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
);
