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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <pjsip.h>

#include "asterisk/res_hep.h"
#include "asterisk/module.h"
#include "asterisk/netsock2.h"
#include "asterisk/stasis.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/json.h"
#include "asterisk/config.h"

static struct stasis_subscription *stasis_rtp_subscription;

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

	capture_info->uuid = ast_strdup(ast_json_string_get(ast_json_object_get(json_channel, "name")));
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

	stasis_rtp_subscription = stasis_subscribe(ast_rtp_topic(),
		rtp_topic_handler, NULL);
	if (!stasis_rtp_subscription) {
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	if (stasis_rtp_subscription) {
		stasis_rtp_subscription = stasis_unsubscribe(stasis_rtp_subscription);
	}

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "RTCP HEPv3 Logger",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_DEFAULT,
	);
