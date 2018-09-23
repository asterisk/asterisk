/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2014, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief RTCP logging with Homer
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 */

/*** MODULEINFO
	<depend>res_hep</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/res_hep.h"
#include "asterisk/module.h"
#include "asterisk/netsock2.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/json.h"
#include "asterisk/config.h"

static struct stasis_subscription *stasis_rtp_subscription;

static char *assign_uuid(struct ast_json *json_channel)
{
	const char *channel_name = ast_json_string_get(ast_json_object_get(json_channel, "name"));
	enum hep_uuid_type uuid_type = hepv3_get_uuid_type();
	char *uuid = NULL;

	if (!channel_name) {
		return NULL;
	}

	if (uuid_type == HEP_UUID_TYPE_CALL_ID) {
		struct ast_channel *chan = NULL;
		char buf[128];

		if (ast_begins_with(channel_name, "PJSIP")) {
			chan = ast_channel_get_by_name(channel_name);

			if (chan && !ast_func_read(chan, "CHANNEL(pjsip,call-id)", buf, sizeof(buf))) {
				uuid = ast_strdup(buf);
			}
		} else if (ast_begins_with(channel_name, "SIP")) {
			chan = ast_channel_get_by_name(channel_name);

			if (chan && !ast_func_read(chan, "SIP_HEADER(call-id)", buf, sizeof(buf))) {
				uuid = ast_strdup(buf);
			}
		}

		ast_channel_cleanup(chan);
	}

	/* If we couldn't get the call-id or didn't want it, just use the channel name */
	if (!uuid) {
		uuid = ast_strdup(channel_name);
	}

	return uuid;
}

static void rtcp_message_handler(struct stasis_message *message)
{

	RAII_VAR(struct ast_json *, json_payload, NULL, ast_json_unref);
	RAII_VAR(char *,  payload, NULL, ast_json_free);
	struct ast_json *json_blob;
	struct ast_json *json_channel;
	struct ast_json *json_rtcp;
	struct hepv3_capture_info *capture_info;
	struct ast_json *from;
	struct ast_json *to;
	struct timeval current_time = ast_tvnow();

	json_payload = stasis_message_to_json(message, NULL);
	if (!json_payload) {
		return;
	}

	json_blob = ast_json_object_get(json_payload, "blob");
	if (!json_blob) {
		return;
	}

	json_channel = ast_json_object_get(json_payload, "channel");
	if (!json_channel) {
		return;
	}

	json_rtcp = ast_json_object_get(json_payload, "rtcp_report");
	if (!json_rtcp) {
		return;
	}

	from = ast_json_object_get(json_blob, "from");
	to = ast_json_object_get(json_blob, "to");
	if (!from || !to) {
		return;
	}

	payload = ast_json_dump_string(json_rtcp);
	if (ast_strlen_zero(payload)) {
		return;
	}

	capture_info = hepv3_create_capture_info(payload, strlen(payload));
	if (!capture_info) {
		return;
	}
	ast_sockaddr_parse(&capture_info->src_addr, ast_json_string_get(from), PARSE_PORT_REQUIRE);
	ast_sockaddr_parse(&capture_info->dst_addr, ast_json_string_get(to), PARSE_PORT_REQUIRE);

	capture_info->uuid = assign_uuid(json_channel);
	if (!capture_info->uuid) {
		ao2_ref(capture_info, -1);
		return;
	}
	capture_info->capture_time = current_time;
	capture_info->capture_type = HEPV3_CAPTURE_TYPE_RTCP;
	capture_info->zipped = 0;

	hepv3_send_packet(capture_info);
}

static void rtp_topic_handler(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	struct stasis_message_type *message_type = stasis_message_type(message);

	if ((message_type == ast_rtp_rtcp_sent_type()) ||
		(message_type == ast_rtp_rtcp_received_type())) {
		rtcp_message_handler(message);
	}
}

static int load_module(void)
{
	if (!hepv3_is_loaded()) {
		ast_log(AST_LOG_WARNING, "res_hep is disabled; declining module load\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	stasis_rtp_subscription = stasis_subscribe(ast_rtp_topic(),
		rtp_topic_handler, NULL);
	if (!stasis_rtp_subscription) {
		return AST_MODULE_LOAD_DECLINE;
	}
	stasis_subscription_accept_message_type(stasis_rtp_subscription, ast_rtp_rtcp_sent_type());
	stasis_subscription_accept_message_type(stasis_rtp_subscription, ast_rtp_rtcp_received_type());
	stasis_subscription_set_filter(stasis_rtp_subscription, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	if (stasis_rtp_subscription) {
		stasis_rtp_subscription = stasis_unsubscribe_and_join(stasis_rtp_subscription);
	}

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "RTCP HEPv3 Logger",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_hep",
);
