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
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_pubsub.h"
#include "asterisk/res_pjsip_presence_xml.h"
#include "asterisk/res_pjsip_body_generator_types.h"

/*!
 * \internal
 * \brief Adds non standard elements to the xml body
 *
 * This is some code that was part of the original chan_sip implementation
 * that is not part of the RFC 3863 definition, but we are keeping available
 * for backward compatability. The original comment stated that Eyebeam
 * supports this format.
 */
static void add_eyebeam(pj_pool_t *pool, pj_xml_node *node, const char *pidfstate)
{
	static const char *XMLNS_PP = "xmlns:pp";
	static const char *XMLNS_PERSON = "urn:ietf:params:xml:ns:pidf:person";

	static const char *XMLNS_ES = "xmlns:es";
	static const char *XMLNS_RPID_STATUS = "urn:ietf:params:xml:ns:pidf:rpid:status:rpid-status";

	static const char *XMLNS_EP = "xmlns:ep";
	static const char *XMLNS_RPID_PERSON = "urn:ietf:params:xml:ns:pidf:rpid:rpid-person";

	pj_xml_node *person = ast_sip_presence_xml_create_node(pool, node, "pp:person");
	pj_xml_node *status = ast_sip_presence_xml_create_node(pool, person, "status");

	if (pidfstate[0] != '-') {
		pj_xml_node *activities = ast_sip_presence_xml_create_node(pool, status, "ep:activities");
		size_t str_size = sizeof("ep:") + strlen(pidfstate);

		activities->content.ptr = pj_pool_alloc(pool, str_size);
		activities->content.slen = pj_ansi_snprintf(activities->content.ptr, str_size,
				"ep:%s", pidfstate);
	}

	ast_sip_presence_xml_create_attr(pool, node, XMLNS_PP, XMLNS_PERSON);
	ast_sip_presence_xml_create_attr(pool, node, XMLNS_ES, XMLNS_RPID_STATUS);
	ast_sip_presence_xml_create_attr(pool, node, XMLNS_EP, XMLNS_RPID_PERSON);
}

static int pidf_supplement_body(void *body, void *data)
{
	pjpidf_pres *pres = body;
	struct ast_sip_exten_state_data *state_data = data;
	char *statestring = NULL, *pidfstate = NULL, *pidfnote = NULL;
	enum ast_sip_pidf_state local_state;

	ast_sip_presence_exten_state_to_str(state_data->exten_state, &statestring,
			&pidfstate, &pidfnote, &local_state);

	add_eyebeam(state_data->pool, pres, pidfstate);
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

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "PJSIP PIDF Eyebeam supplement");
