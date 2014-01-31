/*
 * asterisk -- An open source telephony toolkit.
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

static void *xpidf_allocate_body(void *data)
{
	struct ast_sip_exten_state_data *state_data = data;
	pjxpidf_pres *pres;
	pj_str_t name;

	pres = pjxpidf_create(state_data->pool, pj_cstr(&name, state_data->local));
	return pres;
}

static int xpidf_generate_body_content(void *body, void *data)
{
	pjxpidf_pres *pres = body;
	struct ast_sip_exten_state_data *state_data = data;
	static pj_str_t STR_ADDR_PARAM = { ";user=ip", 8 };
	char *statestring = NULL, *pidfstate = NULL, *pidfnote = NULL;
	pj_xml_attr *attr;
	enum ast_sip_pidf_state local_state;
	pj_str_t uri;
	char sanitized[PJSIP_MAX_URL_SIZE];
	pj_xml_node *atom;
	pj_xml_node *address;
	pj_xml_node *status;
	pj_xml_node *msnsubstatus;

	ast_sip_presence_exten_state_to_str(state_data->exten_state, &statestring,
			&pidfstate, &pidfnote, &local_state);

	ast_sip_presence_xml_find_node_attr(state_data->pool, pres, "atom", "id",
			&atom, &attr);
	pj_strdup2(state_data->pool, &attr->value, state_data->exten);

	ast_sip_presence_xml_find_node_attr(state_data->pool, atom, "address",
			"uri", &address, &attr);

	ast_sip_sanitize_xml(state_data->remote, sanitized, sizeof(sanitized));

	uri.ptr = (char*) pj_pool_alloc(state_data->pool,
			strlen(sanitized) + STR_ADDR_PARAM.slen);
	pj_strcpy2( &uri, sanitized);
	pj_strcat( &uri, &STR_ADDR_PARAM);
	pj_strdup(state_data->pool, &attr->value, &uri);

	ast_sip_presence_xml_create_attr(state_data->pool, address, "priority", "0.80000");

	ast_sip_presence_xml_find_node_attr(state_data->pool, address,
			"status", "status", &status, &attr);
	pj_strdup2(state_data->pool, &attr->value,
		   (local_state ==  NOTIFY_OPEN) ? "open" :
		   (local_state == NOTIFY_INUSE) ? "inuse" : "closed");

	ast_sip_presence_xml_find_node_attr(state_data->pool, address,
			"msnsubstatus", "substatus", &msnsubstatus, &attr);
	pj_strdup2(state_data->pool, &attr->value,
		   (local_state == NOTIFY_OPEN) ? "online" :
		   (local_state == NOTIFY_INUSE) ? "onthephone" : "offline");

	return 0;
}

#define MAX_STRING_GROWTHS 3

static void xpidf_to_string(void *body, struct ast_str **str)
{
	pjxpidf_pres *pres = body;
	int growths = 0;
	int size;

	do {

		size = pjxpidf_print(pres, ast_str_buffer(*str), ast_str_size(*str));
		if (size < 0) {
			ast_str_make_space(str, ast_str_size(*str) * 2);
			++growths;
			return;
		}
	} while (size < 0 && growths < MAX_STRING_GROWTHS);

	if (size < 0) {
		ast_log(LOG_WARNING, "XPIDF body text too large\n");
		return;
	}

	*(ast_str_buffer(*str) + size) = '\0';
	ast_str_update(*str);
}

static struct ast_sip_pubsub_body_generator xpidf_body_generator = {
	.type = "application",
	.subtype = "xpidf+xml",
	.allocate_body = xpidf_allocate_body,
	.generate_body_content = xpidf_generate_body_content,
	.to_string = xpidf_to_string,
	/* No need for a destroy_body callback since we use a pool */
};

static struct ast_sip_pubsub_body_generator cpim_pidf_body_generator = {
	.type = "application",
	.subtype = "cpim-pidf+xml",
	.allocate_body = xpidf_allocate_body,
	.generate_body_content = xpidf_generate_body_content,
	.to_string = xpidf_to_string,
	/* No need for a destroy_body callback since we use a pool */
};

static void unregister_all(void)
{
	ast_sip_pubsub_unregister_body_generator(&cpim_pidf_body_generator);
	ast_sip_pubsub_unregister_body_generator(&xpidf_body_generator);
}

static int load_module(void)
{
	if (ast_sip_pubsub_register_body_generator(&xpidf_body_generator)) {
		goto fail;
	}

	if (ast_sip_pubsub_register_body_generator(&cpim_pidf_body_generator)) {
		goto fail;
	}

	return AST_MODULE_LOAD_SUCCESS;

fail:
	unregister_all();
	return AST_MODULE_LOAD_DECLINE;
}

static int unload_module(void)
{
	unregister_all();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Extension State PIDF Provider",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
