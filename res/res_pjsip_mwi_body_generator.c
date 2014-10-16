/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_pubsub</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_pubsub.h"
#include "asterisk/res_pjsip_body_generator_types.h"
#include "asterisk/module.h"
#include "asterisk/strings.h"

#define MWI_TYPE "application"
#define MWI_SUBTYPE "simple-message-summary"

static void *mwi_allocate_body(void *data)
{
	struct ast_str **mwi_str;

	mwi_str = ast_malloc(sizeof(*mwi_str));
	if (!mwi_str) {
		return NULL;
	}
	*mwi_str = ast_str_create(64);
	if (!*mwi_str) {
		ast_free(mwi_str);
		return NULL;
	}
	return mwi_str;
}

static int mwi_generate_body_content(void *body, void *data)
{
	struct ast_str **mwi = body;
	struct ast_sip_message_accumulator *counter = data;

	ast_str_append(mwi, 0, "Messages-Waiting: %s\r\n",
			counter->new_msgs ? "yes" : "no");
	ast_str_append(mwi, 0, "Voice-Message: %d/%d (0/0)\r\n",
			counter->new_msgs, counter->old_msgs);

	return 0;
}

static void mwi_to_string(void *body, struct ast_str **str)
{
	struct ast_str **mwi = body;

	ast_str_set(str, 0, "%s", ast_str_buffer(*mwi));
}

static void mwi_destroy_body(void *body)
{
	struct ast_str **mwi = body;

	ast_free(*mwi);
	ast_free(mwi);
}

static struct ast_sip_pubsub_body_generator mwi_generator = {
	.type = MWI_TYPE,
	.subtype = MWI_SUBTYPE,
	.body_type = AST_SIP_MESSAGE_ACCUMULATOR,
	.allocate_body = mwi_allocate_body,
	.generate_body_content = mwi_generate_body_content,
	.to_string = mwi_to_string,
	.destroy_body = mwi_destroy_body,
};

static int load_module(void)
{
	CHECK_PJSIP_PUBSUB_MODULE_LOADED();

	if (ast_sip_pubsub_register_body_generator(&mwi_generator)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_pubsub_unregister_body_generator(&mwi_generator);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP MWI resource",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
