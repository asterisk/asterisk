/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009 - 2014, Digium, Inc.
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
 * \brief RTP (Multicast and Unicast) Media Channel
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/acl.h"
#include "asterisk/app.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/causes.h"
#include "asterisk/format_cache.h"

/* Forward declarations */
static struct ast_channel *multicast_rtp_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static struct ast_channel *unicast_rtp_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int rtp_call(struct ast_channel *ast, const char *dest, int timeout);
static int rtp_hangup(struct ast_channel *ast);
static struct ast_frame *rtp_read(struct ast_channel *ast);
static int rtp_write(struct ast_channel *ast, struct ast_frame *f);

/* Multicast channel driver declaration */
static struct ast_channel_tech multicast_rtp_tech = {
	.type = "MulticastRTP",
	.description = "Multicast RTP Paging Channel Driver",
	.requester = multicast_rtp_request,
	.call = rtp_call,
	.hangup = rtp_hangup,
	.read = rtp_read,
	.write = rtp_write,
};

/* Unicast channel driver declaration */
static struct ast_channel_tech unicast_rtp_tech = {
	.type = "UnicastRTP",
	.description = "Unicast RTP Media Channel Driver",
	.requester = unicast_rtp_request,
	.call = rtp_call,
	.hangup = rtp_hangup,
	.read = rtp_read,
	.write = rtp_write,
};

/*! \brief Function called when we should read a frame from the channel */
static struct ast_frame  *rtp_read(struct ast_channel *ast)
{
	struct ast_rtp_instance *instance = ast_channel_tech_pvt(ast);
	int fdno = ast_channel_fdno(ast);

	switch (fdno) {
	case 0:
		return ast_rtp_instance_read(instance, 0);
	default:
		return &ast_null_frame;
	}
}

/*! \brief Function called when we should write a frame to the channel */
static int rtp_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct ast_rtp_instance *instance = ast_channel_tech_pvt(ast);

	return ast_rtp_instance_write(instance, f);
}

/*! \brief Function called when we should actually call the destination */
static int rtp_call(struct ast_channel *ast, const char *dest, int timeout)
{
	struct ast_rtp_instance *instance = ast_channel_tech_pvt(ast);

	ast_queue_control(ast, AST_CONTROL_ANSWER);

	return ast_rtp_instance_activate(instance);
}

/*! \brief Function called when we should hang the channel up */
static int rtp_hangup(struct ast_channel *ast)
{
	struct ast_rtp_instance *instance = ast_channel_tech_pvt(ast);

	ast_rtp_instance_destroy(instance);

	ast_channel_tech_pvt_set(ast, NULL);

	return 0;
}

/*! \brief Function called when we should prepare to call the multicast destination */
static struct ast_channel *multicast_rtp_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	char *parse;
	struct ast_rtp_instance *instance;
	struct ast_sockaddr control_address;
	struct ast_sockaddr destination_address;
	struct ast_channel *chan;
	struct ast_format_cap *caps = NULL;
	struct ast_format *fmt = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(type);
		AST_APP_ARG(destination);
		AST_APP_ARG(control);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "A multicast type and destination must be given to the 'MulticastRTP' channel\n");
		goto failure;
	}
	parse = ast_strdupa(data);
	AST_NONSTANDARD_APP_ARGS(args, parse, '/');

	fmt = ast_format_cap_get_format(cap, 0);

	ast_sockaddr_setnull(&control_address);

	if (!ast_strlen_zero(args.control) &&
		!ast_sockaddr_parse(&control_address, args.control, PARSE_PORT_REQUIRE)) {
		ast_log(LOG_ERROR, "Control address '%s' could not be parsed\n", args.control);
		goto failure;
	}

	if (!ast_sockaddr_parse(&destination_address, args.destination,
				PARSE_PORT_REQUIRE)) {
		ast_log(LOG_ERROR, "Destination address '%s' could not be parsed\n", args.destination);
		goto failure;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		goto failure;
	}

	if (!(instance = ast_rtp_instance_new("multicast", NULL, &control_address, args.type))) {
		ast_log(LOG_ERROR, "Could not create RTP instance for sending media to '%s'\n", args.destination);
		goto failure;
	}

	if (!(chan = ast_channel_alloc(1, AST_STATE_DOWN, "", "", "", "", "", assignedids, requestor, 0, "MulticastRTP/%p", instance))) {
		ast_rtp_instance_destroy(instance);
		goto failure;
	}
	ast_rtp_instance_set_channel_id(instance, ast_channel_uniqueid(chan));
	ast_rtp_instance_set_remote_address(instance, &destination_address);

	ast_channel_tech_set(chan, &multicast_rtp_tech);

	ast_format_cap_append(caps, fmt, 0);
	ast_channel_nativeformats_set(chan, caps);
	ast_channel_set_writeformat(chan, fmt);
	ast_channel_set_rawwriteformat(chan, fmt);
	ast_channel_set_readformat(chan, fmt);
	ast_channel_set_rawreadformat(chan, fmt);

	ast_channel_tech_pvt_set(chan, instance);

	ast_channel_unlock(chan);

	ao2_ref(fmt, -1);
	ao2_ref(caps, -1);

	return chan;

failure:
	ao2_cleanup(fmt);
	ao2_cleanup(caps);
	*cause = AST_CAUSE_FAILURE;
	return NULL;
}

/*! \brief Function called when we should prepare to call the unicast destination */
static struct ast_channel *unicast_rtp_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	char *parse;
	struct ast_rtp_instance *instance;
	struct ast_sockaddr address;
	struct ast_sockaddr local_address;
	struct ast_channel *chan;
	struct ast_format_cap *caps = NULL;
	struct ast_format *fmt = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(destination);
		AST_APP_ARG(engine);
		AST_APP_ARG(format);
	);

	if (ast_strlen_zero(data)) {
		goto failure;
	}
	parse = ast_strdupa(data);
	AST_NONSTANDARD_APP_ARGS(args, parse, '/');

	if (!ast_strlen_zero(args.format)) {
		fmt = ast_format_cache_get(args.format);
	} else {
		fmt = ast_format_cap_get_format(cap, 0);
	}

	if (!fmt) {
		ast_log(LOG_ERROR, "No format specified for sending RTP to '%s'\n", args.destination);
		goto failure;
	}

	if (!ast_sockaddr_parse(&address, args.destination,
				PARSE_PORT_REQUIRE)) {
		ast_log(LOG_ERROR, "Destination '%s' could not be parsed\n", args.destination);
		goto failure;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		goto failure;
	}

	ast_ouraddrfor(&address, &local_address);
	if (!(instance = ast_rtp_instance_new(args.engine, NULL, &local_address, NULL))) {
		ast_log(LOG_ERROR, "Could not create RTP instance for sending media to '%s'\n", args.destination);
		goto failure;
	}

	if (!(chan = ast_channel_alloc(1, AST_STATE_DOWN, "", "", "", "", "", assignedids, requestor, 0, "UnicastRTP/%s-%p", args.destination, instance))) {
		ast_rtp_instance_destroy(instance);
		goto failure;
	}
	ast_rtp_instance_set_channel_id(instance, ast_channel_uniqueid(chan));
	ast_rtp_instance_set_remote_address(instance, &address);
	ast_channel_set_fd(chan, 0, ast_rtp_instance_fd(instance, 0));

	ast_channel_tech_set(chan, &unicast_rtp_tech);

	ast_format_cap_append(caps, fmt, 0);
	ast_channel_nativeformats_set(chan, caps);
	ast_channel_set_writeformat(chan, fmt);
	ast_channel_set_rawwriteformat(chan, fmt);
	ast_channel_set_readformat(chan, fmt);
	ast_channel_set_rawreadformat(chan, fmt);

	ast_channel_tech_pvt_set(chan, instance);

	pbx_builtin_setvar_helper(chan, "UNICASTRTP_LOCAL_ADDRESS", ast_sockaddr_stringify_addr(&local_address));
	ast_rtp_instance_get_local_address(instance, &local_address);
	pbx_builtin_setvar_helper(chan, "UNICASTRTP_LOCAL_PORT", ast_sockaddr_stringify_port(&local_address));

	ast_channel_unlock(chan);

	ao2_ref(fmt, -1);
	ao2_ref(caps, -1);

	return chan;

failure:
	ao2_cleanup(fmt);
	ao2_cleanup(caps);
	*cause = AST_CAUSE_FAILURE;
	return NULL;
}

/*! \brief Function called when our module is unloaded */
static int unload_module(void)
{
	ast_channel_unregister(&multicast_rtp_tech);
	ao2_cleanup(multicast_rtp_tech.capabilities);
	multicast_rtp_tech.capabilities = NULL;

	ast_channel_unregister(&unicast_rtp_tech);
	ao2_cleanup(unicast_rtp_tech.capabilities);
	unicast_rtp_tech.capabilities = NULL;

	return 0;
}

/*! \brief Function called when our module is loaded */
static int load_module(void)
{
	if (!(multicast_rtp_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append_by_type(multicast_rtp_tech.capabilities, AST_MEDIA_TYPE_UNKNOWN);
	if (ast_channel_register(&multicast_rtp_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'MulticastRTP'\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(unicast_rtp_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append_by_type(unicast_rtp_tech.capabilities, AST_MEDIA_TYPE_UNKNOWN);
	if (ast_channel_register(&unicast_rtp_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'UnicastRTP'\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "RTP Media Channel",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
);
