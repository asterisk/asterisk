/*
 * asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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

/*** MODULEINFO
	<load_priority>channel_depend</load_priority>
	<depend>pjproject</depend>
	<use type="module">res_pjsip</use>
	<use type="module">res_pjsip_pubsub</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjlib.h>

#include "asterisk/module.h"
#include "asterisk/presencestate.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_pubsub.h"
#include "asterisk/res_pjsip_presence_xml.h"
#include "asterisk/res_pjsip_body_generator_types.h"

static int pidf_supplement_body(void *body, void *data)
{
	struct ast_sip_exten_state_data *state_data = data;
	pj_xml_node *node;
	char sanitized[256];

	if (ast_strlen_zero(state_data->user_agent) ||
	    !strstr(state_data->user_agent, "digium")) {
		/* not a digium phone */
		return 0;
	}

	if (!(node = ast_sip_presence_xml_create_node(
		      state_data->pool, body, "tuple"))) {
		ast_log(LOG_WARNING, "Unable to create PIDF tuple\n");
		return -1;
	}

	ast_sip_presence_xml_create_attr(
		state_data->pool, node, "id", "digium-presence");

	if (!(node = ast_sip_presence_xml_create_node(
		      state_data->pool, node, "status"))) {
		ast_log(LOG_WARNING, "Unable to create PIDF tuple status\n");
		return -1;
	}

	if (!(node = ast_sip_presence_xml_create_node(
		      state_data->pool, node, "digium_presence"))) {
		ast_log(LOG_WARNING, "Unable to create digium presence\n");
		return -1;
	}

	if (!ast_strlen_zero(state_data->presence_message)) {
		ast_sip_sanitize_xml(state_data->presence_message, sanitized, sizeof(sanitized));
		pj_strdup2(state_data->pool, &node->content, sanitized);
	}

	ast_sip_presence_xml_create_attr(
		state_data->pool, node, "type", ast_presence_state2str(
			state_data->presence_state));

	if (!ast_strlen_zero(state_data->presence_subtype)) {
		ast_sip_sanitize_xml(state_data->presence_subtype, sanitized, sizeof(sanitized));
		ast_sip_presence_xml_create_attr(
			state_data->pool, node, "subtype", sanitized);
	}

	return 0;
}

static struct ast_sip_pubsub_body_supplement pidf_supplement = {
	.type = "application",
	.subtype = "pidf+xml",
	.supplement_body = pidf_supplement_body,
};

static int load_module(void)
{
	if (ast_sip_pubsub_register_body_supplement(&pidf_supplement)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static void unload_module(void)
{
	ast_sip_pubsub_unregister_body_supplement(&pidf_supplement);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "PJSIP PIDF Digium presence supplement");
