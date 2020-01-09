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

static int dialog_info_generate_body_content(void *body, void *data)
{
	pj_xml_node *dialog_info = body, *dialog, *state;
	struct ast_datastore *datastore;
	struct dialog_info_xml_state *datastore_state;
	struct ast_sip_exten_state_data *state_data = data;
	char *local = ast_strdupa(state_data->local), *stripped, *statestring = NULL;
	char *pidfstate = NULL, *pidfnote = NULL;
	enum ast_sip_pidf_state local_state;
	char version_str[32], sanitized[PJSIP_MAX_URL_SIZE];
	struct ast_sip_endpoint *endpoint = NULL;
	unsigned int notify_early_inuse_ringing = 0;

	if (!local || !state_data->datastores) {
		return -1;
	}

	datastore = ast_datastores_find(state_data->datastores, "dialog-info+xml");
	if (!datastore) {
		const struct ast_json *version_json = NULL;

		datastore = ast_datastores_alloc_datastore(&dialog_info_xml_datastore, "dialog-info+xml");
		if (!datastore) {
			return -1;
		}

		datastore->data = ast_calloc(1, sizeof(struct dialog_info_xml_state));
		if (!datastore->data || ast_datastores_add(state_data->datastores, datastore)) {
			ao2_ref(datastore, -1);
			return -1;
		}
		datastore_state = datastore->data;

		if (state_data->sub) {
			version_json = ast_sip_subscription_get_persistence_data(state_data->sub);
		}

		if (version_json) {
			datastore_state->version = ast_json_integer_get(version_json);
			datastore_state->version++;
		} else {
			datastore_state->version = 0;
		}
	} else {
		datastore_state = datastore->data;
		datastore_state->version++;
	}

	stripped = ast_strip_quoted(local, "<", ">");
	ast_sip_sanitize_xml(stripped, sanitized, sizeof(sanitized));

	if (state_data->sub && (endpoint = ast_sip_subscription_get_endpoint(state_data->sub))) {
	    notify_early_inuse_ringing = endpoint->notify_early_inuse_ringing;
	    ao2_cleanup(endpoint);
	}
	ast_sip_presence_exten_state_to_str(state_data->exten_state, &statestring,
			&pidfstate, &pidfnote, &local_state, notify_early_inuse_ringing);

	ast_sip_presence_xml_create_attr(state_data->pool, dialog_info, "xmlns", "urn:ietf:params:xml:ns:dialog-info");

	snprintf(version_str, sizeof(version_str), "%u", datastore_state->version);
	ast_sip_presence_xml_create_attr(state_data->pool, dialog_info, "version", version_str);

	if (state_data->sub) {
		ast_sip_subscription_set_persistence_data(state_data->sub, ast_json_integer_create(datastore_state->version));
	}

	ast_sip_presence_xml_create_attr(state_data->pool, dialog_info, "state", "full");
	ast_sip_presence_xml_create_attr(state_data->pool, dialog_info, "entity", sanitized);

	dialog = ast_sip_presence_xml_create_node(state_data->pool, dialog_info, "dialog");
	ast_sip_presence_xml_create_attr(state_data->pool, dialog, "id", state_data->exten);
	if (!ast_strlen_zero(statestring) && !strcmp(statestring, "early")) {
		ast_sip_presence_xml_create_attr(state_data->pool, dialog, "direction", "recipient");
	}

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

	ao2_ref(datastore, -1);

	return 0;
}

/* The maximum number of times the ast_str() for the body text can grow before we declare an XML body
 * too large to send.
 */
#define MAX_STRING_GROWTHS 6

static void dialog_info_to_string(void *body, struct ast_str **str)
{
	pj_xml_node *dialog_info = body;
	int growths = 0;
	int size;

	do {
		size = pj_xml_print(dialog_info, ast_str_buffer(*str), ast_str_size(*str) - 1, PJ_TRUE);
		if (size <= AST_PJSIP_XML_PROLOG_LEN) {
			ast_str_make_space(str, ast_str_size(*str) * 2);
			++growths;
		}
	} while (size <= AST_PJSIP_XML_PROLOG_LEN && growths < MAX_STRING_GROWTHS);
	if (size <= AST_PJSIP_XML_PROLOG_LEN) {
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
	.requires = "res_pjsip,res_pjsip_pubsub",
);
