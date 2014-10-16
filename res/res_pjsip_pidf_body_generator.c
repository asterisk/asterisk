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

static void *pidf_allocate_body(void *data)
{
	struct ast_sip_exten_state_data *state_data = data;
	char *local = ast_strdupa(state_data->local);
	pjpidf_pres *pres;
	pj_str_t entity;

	pres = pjpidf_create(state_data->pool, pj_cstr(&entity, ast_strip_quoted(local, "<", ">")));

	return pres;
}

static int pidf_generate_body_content(void *body, void *data)
{
	pjpidf_tuple *tuple;
	pj_str_t note, id, contact, priority;
	char *statestring = NULL, *pidfstate = NULL, *pidfnote = NULL;
	enum ast_sip_pidf_state local_state;
	char sanitized[PJSIP_MAX_URL_SIZE];
	pjpidf_pres *pres = body;
	struct ast_sip_exten_state_data *state_data = data;

	ast_sip_presence_exten_state_to_str(state_data->exten_state, &statestring,
			&pidfstate, &pidfnote, &local_state);

	if (!pjpidf_pres_add_note(state_data->pool, pres, pj_cstr(&note, pidfnote))) {
		ast_log(LOG_WARNING, "Unable to add note to PIDF presence\n");
		return -1;
	}

	if (!(tuple = pjpidf_pres_add_tuple(state_data->pool, pres,
					pj_cstr(&id, state_data->exten)))) {
		ast_log(LOG_WARNING, "Unable to create PIDF tuple\n");
		return -1;
	}

	ast_sip_sanitize_xml(state_data->remote, sanitized, sizeof(sanitized));
	pjpidf_tuple_set_contact(state_data->pool, tuple, pj_cstr(&contact, sanitized));
	pjpidf_tuple_set_contact_prio(state_data->pool, tuple, pj_cstr(&priority, "1"));
	pjpidf_status_set_basic_open(pjpidf_tuple_get_status(tuple),
			local_state == NOTIFY_OPEN);

	return 0;
}

#define MAX_STRING_GROWTHS 5

static void pidf_to_string(void *body, struct ast_str **str)
{
	int size;
	int growths = 0;
	pjpidf_pres *pres = body;

	do {
		size = pjpidf_print(pres, ast_str_buffer(*str), ast_str_size(*str) - 1);
		if (size == AST_PJSIP_XML_PROLOG_LEN) {
			ast_str_make_space(str, ast_str_size(*str) * 2);
			++growths;
		}
	} while (size == AST_PJSIP_XML_PROLOG_LEN && growths < MAX_STRING_GROWTHS);

	if (size == AST_PJSIP_XML_PROLOG_LEN) {
		ast_log(LOG_WARNING, "PIDF body text too large\n");
		return;
	}

	*(ast_str_buffer(*str) + size) = '\0';
	ast_str_update(*str);
}

static struct ast_sip_pubsub_body_generator pidf_body_generator = {
	.type = "application",
	.subtype = "pidf+xml",
	.body_type = AST_SIP_EXTEN_STATE_DATA,
	.allocate_body = pidf_allocate_body,
	.generate_body_content = pidf_generate_body_content,
	.to_string = pidf_to_string,
	/* No need for a destroy_body callback since we use a pool */
};

static int load_module(void)
{
	CHECK_PJSIP_PUBSUB_MODULE_LOADED();

	if (ast_sip_pubsub_register_body_generator(&pidf_body_generator)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_pubsub_unregister_body_generator(&pidf_body_generator);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Extension State PIDF Provider",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
