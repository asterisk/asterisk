/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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

/*!
 * \file
 * \brief Bridge Media Channels driver
 *
 * \author Jonathan Rose <jrose@digium.com>
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * \brief Bridge Media Channels
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/core_unreal.h"
#include "asterisk/module.h"

static int media_call(struct ast_channel *chan, const char *addr, int timeout)
{
	/* ast_call() will fail unconditionally against channels provided by this driver */
	return -1;
}

static int media_hangup(struct ast_channel *ast)
{
	struct ast_unreal_pvt *p = ast_channel_tech_pvt(ast);
	int res;

	if (!p) {
		return -1;
	}

	/* Give the pvt a ref to fulfill calling requirements. */
	ao2_ref(p, +1);
	res = ast_unreal_hangup(p, ast);
	ao2_ref(p, -1);

	return res;
}

static struct ast_channel *announce_request(const char *type, struct ast_format_cap *cap,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);

static struct ast_channel *record_request(const char *type, struct ast_format_cap *cap,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);

static struct ast_channel_tech announce_tech = {
	.type = "Announcer",
	.description = "Bridge Media Announcing Channel Driver",
	.requester = announce_request,
	.call = media_call,
	.hangup = media_hangup,

	.send_digit_begin = ast_unreal_digit_begin,
	.send_digit_end = ast_unreal_digit_end,
	.read = ast_unreal_read,
	.write = ast_unreal_write,
	.write_video = ast_unreal_write,
	.exception = ast_unreal_read,
	.indicate = ast_unreal_indicate,
	.fixup = ast_unreal_fixup,
	.send_html = ast_unreal_sendhtml,
	.send_text = ast_unreal_sendtext,
	.queryoption = ast_unreal_queryoption,
	.setoption = ast_unreal_setoption,
	.properties = AST_CHAN_TP_INTERNAL,
};

static struct ast_channel_tech record_tech = {
	.type = "Recorder",
	.description = "Bridge Media Recording Channel Driver",
	.requester = record_request,
	.call = media_call,
	.hangup = media_hangup,

	.send_digit_begin = ast_unreal_digit_begin,
	.send_digit_end = ast_unreal_digit_end,
	.read = ast_unreal_read,
	.write = ast_unreal_write,
	.write_video = ast_unreal_write,
	.exception = ast_unreal_read,
	.indicate = ast_unreal_indicate,
	.fixup = ast_unreal_fixup,
	.send_html = ast_unreal_sendhtml,
	.send_text = ast_unreal_sendtext,
	.queryoption = ast_unreal_queryoption,
	.setoption = ast_unreal_setoption,
	.properties = AST_CHAN_TP_INTERNAL,
};

static struct ast_channel *media_request_helper(struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids,
	const struct ast_channel *requestor, const char *data, struct ast_channel_tech *tech, const char *role)
{
	struct ast_channel *chan;
	ast_callid callid;

	RAII_VAR(struct ast_unreal_pvt *, pvt, NULL, ao2_cleanup);

	if (!(pvt = ast_unreal_alloc(sizeof(*pvt), ast_unreal_destructor, cap))) {
		return NULL;
	}

	ast_copy_string(pvt->name, data, sizeof(pvt->name));

	ast_set_flag(pvt, AST_UNREAL_NO_OPTIMIZATION);

	callid = ast_read_threadstorage_callid();

	chan = ast_unreal_new_channels(pvt, tech,
		AST_STATE_UP, AST_STATE_UP, NULL, NULL, assignedids, requestor, callid);
	if (!chan) {
		return NULL;
	}

	ast_answer(pvt->owner);
	ast_answer(pvt->chan);

	if (ast_channel_add_bridge_role(pvt->chan, role)) {
		ast_hangup(chan);
		return NULL;
	}

	return chan;
}

static struct ast_channel *announce_request(const char *type, struct ast_format_cap *cap,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	return media_request_helper(cap, assignedids, requestor, data, &announce_tech, "announcer");
}

static struct ast_channel *record_request(const char *type, struct ast_format_cap *cap,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	return media_request_helper(cap, assignedids, requestor, data, &record_tech, "recorder");
}

static void cleanup_capabilities(void)
{
	if (announce_tech.capabilities) {
		ao2_ref(announce_tech.capabilities, -1);
		announce_tech.capabilities = NULL;
	}

	if (record_tech.capabilities) {
		ao2_ref(record_tech.capabilities, -1);
		record_tech.capabilities = NULL;
	}
}

static int unload_module(void)
{
	ast_channel_unregister(&announce_tech);
	ast_channel_unregister(&record_tech);
	cleanup_capabilities();
	return 0;
}

static int load_module(void)
{
	announce_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!announce_tech.capabilities) {
		return AST_MODULE_LOAD_DECLINE;
	}

	record_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!record_tech.capabilities) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_format_cap_append_by_type(announce_tech.capabilities, AST_MEDIA_TYPE_UNKNOWN);
	ast_format_cap_append_by_type(record_tech.capabilities, AST_MEDIA_TYPE_UNKNOWN);

	if (ast_channel_register(&announce_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel technology %s(%s).\n",
			announce_tech.type, announce_tech.description);
		cleanup_capabilities();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_channel_register(&record_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel technology %s(%s).\n",
			record_tech.type, record_tech.description);
		cleanup_capabilities();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Bridge Media Channel Driver",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
);
