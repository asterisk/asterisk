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
	static const char *XMLNS_DM_PREFIX = "xmlns:dm";
	static const char *XMLNS_DM = "urn:ietf:params:xml:ns:pidf:data-model";

	static const char *XMLNS_RPID_PREFIX = "xmlns:rpid";
	static const char *XMLNS_RPID = "urn:ietf:params:xml:ns:pidf:rpid";

	pj_xml_node *person = ast_sip_presence_xml_create_node(pool, node, "dm:person");

	if (pidfstate[0] != '-') {
		pj_xml_node *activities = ast_sip_presence_xml_create_node(pool, person, "rpid:activities");
		size_t str_size = sizeof("rpid:") + strlen(pidfstate);
		char *act_str = ast_alloca(str_size);

		/* Safe */
		strcpy(act_str, "rpid:");
		strcat(act_str, pidfstate);

		ast_sip_presence_xml_create_node(pool, activities, act_str);
	}

	ast_sip_presence_xml_create_attr(pool, node, XMLNS_DM_PREFIX, XMLNS_DM);
	ast_sip_presence_xml_create_attr(pool, node, XMLNS_RPID_PREFIX, XMLNS_RPID);
}

static int pidf_supplement_body(void *body, void *data)
{
	pjpidf_pres *pres = body;
	struct ast_sip_exten_state_data *state_data = data;
	char *statestring = NULL, *pidfstate = NULL, *pidfnote = NULL;
	enum ast_sip_pidf_state local_state;

	ast_sip_presence_exten_state_to_str(state_data->exten_state, &statestring,
			&pidfstate, &pidfnote, &local_state, 0);

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
	CHECK_PJSIP_PUBSUB_MODULE_LOADED();

	if (ast_sip_pubsub_register_body_supplement(&pidf_supplement)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_pubsub_unregister_body_supplement(&pidf_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP PIDF Eyebeam supplement",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
