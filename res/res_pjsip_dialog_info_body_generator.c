/*
 * asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_pubsub</depend>
	<depend>res_pjsip_exten_state</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjlib.h>

#include "asterisk/module.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_pubsub.h"
#include "asterisk/res_pjsip_presence_xml.h"
#include "asterisk/res_pjsip_body_generator_types.h"

/*! \brief Structure which contains dialog-info+xml state information */
struct dialog_info_xml_state {
	/*! \brief Version to place into the next NOTIFY */
	unsigned int version;
};

/*! \brief Destructor for dialog-info+xml information */
static void dialog_info_xml_state_destroy(void *obj)
{
	ast_free(obj);
}

/*! \brief Datastore for attaching dialog-info+xml state information */
static const struct ast_datastore_info dialog_info_xml_datastore = {
	.type = "dialog-info+xml",
	.destroy = dialog_info_xml_state_destroy,
};

static void *dialog_info_allocate_body(void *data)
{
	struct ast_sip_exten_state_data *state_data = data;

	return ast_sip_presence_xml_create_node(state_data->pool, NULL, "dialog-info");
}

static struct ast_datastore *dialog_info_xml_state_find_or_create(struct ast_sip_subscription *sub)
{
	struct ast_datastore *datastore = ast_sip_subscription_get_datastore(sub, "dialog-info+xml");

	if (datastore) {
		return datastore;
	}

	datastore = ast_sip_subscription_alloc_datastore(&dialog_info_xml_datastore, "dialog-info+xml");
	if (!datastore) {
		return NULL;
	}
	datastore->data = ast_calloc(1, sizeof(struct dialog_info_xml_state));
	if (!datastore->data || ast_sip_subscription_add_datastore(sub, datastore)) {
		ao2_ref(datastore, -1);
		return NULL;
	}

	return datastore;
}

static unsigned int dialog_info_xml_get_version(struct ast_sip_subscription *sub, unsigned int *version)
{
	struct ast_datastore *datastore = dialog_info_xml_state_find_or_create(sub);
	struct dialog_info_xml_state *state;

	if (!datastore) {
		return -1;
	}

	state = datastore->data;
	*version = state->version++;
	ao2_ref(datastore, -1);

	return 0;
}

static int dialog_info_generate_body_content(void *body, void *data)
{
	pj_xml_node *dialog_info = body, *dialog, *state;
	struct ast_sip_exten_state_data *state_data = data;
	char *local = ast_strdupa(state_data->local), *stripped, *statestring = NULL;
	char *pidfstate = NULL, *pidfnote = NULL;
	enum ast_sip_pidf_state local_state;
	unsigned int version;
	char version_str[32], sanitized[PJSIP_MAX_URL_SIZE];

	if (!local || !state_data->sub) {
		return -1;
	}

	if (dialog_info_xml_get_version(state_data->sub, &version)) {
		ast_log(LOG_WARNING, "dialog-info+xml version could not be retrieved from datastore\n");
		return -1;
	}

	stripped = ast_strip_quoted(local, "<", ">");
	ast_sip_sanitize_xml(stripped, sanitized, sizeof(sanitized));

	ast_sip_presence_exten_state_to_str(state_data->exten_state, &statestring,
			&pidfstate, &pidfnote, &local_state);

	ast_sip_presence_xml_create_attr(state_data->pool, dialog_info, "xmlns", "urn:ietf:params:xml:ns:dialog-info");

	snprintf(version_str, sizeof(version_str), "%u", version);
	ast_sip_presence_xml_create_attr(state_data->pool, dialog_info, "version", version_str);

	ast_sip_presence_xml_create_attr(state_data->pool, dialog_info, "state", "full");
	ast_sip_presence_xml_create_attr(state_data->pool, dialog_info, "entity", sanitized);

	dialog = ast_sip_presence_xml_create_node(state_data->pool, dialog_info, "dialog");
	ast_sip_presence_xml_create_attr(state_data->pool, dialog, "id", state_data->exten);

	state = ast_sip_presence_xml_create_node(state_data->pool, dialog, "state");
	pj_strdup2(state_data->pool, &state->content, statestring);

	if (state_data->exten_state == AST_EXTENSION_ONHOLD) {
		pj_xml_node *local_node, *target, *param;

		local_node = ast_sip_presence_xml_create_node(state_data->pool, dialog, "local");
		target = ast_sip_presence_xml_create_node(state_data->pool, local_node, "target");
		ast_sip_presence_xml_create_attr(state_data->pool, target, "uri", sanitized);
		param = ast_sip_presence_xml_create_node(state_data->pool, target, "param");
		ast_sip_presence_xml_create_attr(state_data->pool, param, "pname", "+sip.rendering");
		ast_sip_presence_xml_create_attr(state_data->pool, param, "pvalue", "no");
	}

	return 0;
}

/* The maximum number of times the ast_str() for the body text can grow before we declare an XML body
 * too large to send.
 */
#define MAX_STRING_GROWTHS 3

static void dialog_info_to_string(void *body, struct ast_str **str)
{
	pj_xml_node *dialog_info = body;
	int growths = 0;
	int size;

	do {
		size = pj_xml_print(dialog_info, ast_str_buffer(*str), ast_str_size(*str), PJ_TRUE);
		if (size == AST_PJSIP_XML_PROLOG_LEN) {
			ast_str_make_space(str, ast_str_size(*str) * 2);
			++growths;
		}
	} while (size == AST_PJSIP_XML_PROLOG_LEN && growths < MAX_STRING_GROWTHS);

	if (size == AST_PJSIP_XML_PROLOG_LEN) {
		ast_log(LOG_WARNING, "dialog-info+xml body text too large\n");
		return;
	}

	*(ast_str_buffer(*str) + size) = '\0';
	ast_str_update(*str);
}

static struct ast_sip_pubsub_body_generator dialog_info_body_generator = {
	.type = "application",
	.subtype = "dialog-info+xml",
	.body_type = AST_SIP_EXTEN_STATE_DATA,
	.allocate_body = dialog_info_allocate_body,
	.generate_body_content = dialog_info_generate_body_content,
	.to_string = dialog_info_to_string,
	/* No need for a destroy_body callback since we use a pool */
};

static int load_module(void)
{
	CHECK_PJSIP_PUBSUB_MODULE_LOADED();

	if (ast_sip_pubsub_register_body_generator(&dialog_info_body_generator)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_pubsub_unregister_body_generator(&dialog_info_body_generator);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Extension State Dialog Info+XML Provider",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
