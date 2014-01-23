/*
 * asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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
#include "asterisk/res_pjsip_exten_state.h"

enum state {
	NOTIFY_OPEN,
	NOTIFY_INUSE,
	NOTIFY_CLOSED
};

static void exten_state_to_str(int state, char **statestring, char **pidfstate,
			       char **pidfnote, int *local_state)
{
	switch (state) {
	case AST_EXTENSION_RINGING:
		*statestring = "early";
		*local_state = NOTIFY_INUSE;
		*pidfstate = "busy";
		*pidfnote = "Ringing";
		break;
	case AST_EXTENSION_INUSE:
		*statestring = "confirmed";
		*local_state = NOTIFY_INUSE;
		*pidfstate = "busy";
		*pidfnote = "On the phone";
		break;
	case AST_EXTENSION_BUSY:
		*statestring = "confirmed";
		*local_state = NOTIFY_CLOSED;
		*pidfstate = "busy";
		*pidfnote = "On the phone";
		break;
	case AST_EXTENSION_UNAVAILABLE:
		*statestring = "terminated";
		*local_state = NOTIFY_CLOSED;
		*pidfstate = "away";
		*pidfnote = "Unavailable";
		break;
	case AST_EXTENSION_ONHOLD:
		*statestring = "confirmed";
		*local_state = NOTIFY_CLOSED;
		*pidfstate = "busy";
		*pidfnote = "On hold";
		break;
	case AST_EXTENSION_NOT_INUSE:
	default:
		/* Default setting */
		*statestring = "terminated";
		*local_state = NOTIFY_OPEN;
		*pidfstate = "--";
		*pidfnote ="Ready";

		break;
	}
}

static pj_xml_attr *create_attr(pj_pool_t *pool, pj_xml_node *node,
				const char *name, const char *value)
{
	pj_xml_attr *attr = PJ_POOL_ALLOC_T(pool, pj_xml_attr);

	pj_strdup2(pool, &attr->name, name);
	pj_strdup2(pool, &attr->value, value);

	pj_xml_add_attr(node, attr);
	return attr;
}

static pj_xml_node *create_node(pj_pool_t *pool, pj_xml_node *parent,
				const char* name)
{
	pj_xml_node *node = PJ_POOL_ALLOC_T(pool, pj_xml_node);

	pj_list_init(&node->attr_head);
	pj_list_init(&node->node_head);

	pj_strdup2(pool, &node->name, name);

	node->content.ptr = NULL;
	node->content.slen = 0;

	pj_xml_add_node(parent, node);
	return node;
}

static void find_node_attr(pj_pool_t* pool, pj_xml_node *parent,
				   const char *node_name, const char *attr_name,
				   pj_xml_node **node, pj_xml_attr **attr)
{
	pj_str_t name;

	if (!(*node = pj_xml_find_node(parent, pj_cstr(&name, node_name)))) {
		*node = create_node(pool, parent, node_name);
	}

	if (!(*attr = pj_xml_find_attr(*node, pj_cstr(&name, attr_name), NULL))) {
		*attr = create_attr(pool, *node, attr_name, "");
	}
}

/*!
 * \internal
 * \brief Adds non standard elements to the xml body
 *
 * This is some code that was part of the original chan_sip implementation
 * that is not part of the RFC 3863 definition, but we are keeping available
 * for backward compatability. The original comment stated that Eyebeam
 * supports this format.

 */
static void add_non_standard(pj_pool_t *pool, pj_xml_node *node, const char *pidfstate)
{
	static const char *XMLNS_PP = "xmlns:pp";
	static const char *XMLNS_PERSON = "urn:ietf:params:xml:ns:pidf:person";

	static const char *XMLNS_ES = "xmlns:es";
	static const char *XMLNS_RPID_STATUS = "urn:ietf:params:xml:ns:pidf:rpid:status:rpid-status";

	static const char *XMLNS_EP = "xmlns:ep";
	static const char *XMLNS_RPID_PERSON = "urn:ietf:params:xml:ns:pidf:rpid:rpid-person";

	pj_xml_node *person = create_node(pool, node, "pp:person");
	pj_xml_node *status = create_node(pool, person, "status");

	if (pidfstate[0] != '-') {
		pj_xml_node *activities = create_node(pool, status, "ep:activities");
		size_t str_size = sizeof("ep:") + strlen(pidfstate);

		activities->content.ptr = pj_pool_alloc(pool, str_size);
		activities->content.slen = pj_ansi_snprintf(activities->content.ptr, str_size,
				"ep:%s", pidfstate);
	}

	create_attr(pool, node, XMLNS_PP, XMLNS_PERSON);
	create_attr(pool, node, XMLNS_ES, XMLNS_RPID_STATUS);
	create_attr(pool, node, XMLNS_EP, XMLNS_RPID_PERSON);
}

static void release_pool(void *obj)
{
	pj_pool_t *pool = obj;

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
}

/*!
 * \internal
 * \brief Convert angle brackets in input into escaped forms suitable for XML
 *
 * \param input Raw input string
 * \param output Sanitized string
 * \param len Size of output buffer
 */
static void sanitize_xml(const char *input, char *output, size_t len)
{
	char *copy = ast_strdupa(input);
	char *break_point;

	output[0] = '\0';

	while ((break_point = strpbrk(copy, "<>\"&'"))) {
		char to_escape = *break_point;

		*break_point = '\0';
		strncat(output, copy, len);

		switch (to_escape) {
		case '<':
			strncat(output, "&lt;", len);
			break;
		case '>':
			strncat(output, "&gt;", len);
			break;
		case '"':
			strncat(output, "&quot;", len);
			break;
		case '&':
			strncat(output, "&amp;", len);
			break;
		case '\'':
			strncat(output, "&apos;", len);
			break;
		};

		copy = break_point + 1;
	}

	/* Be sure to copy everything after the final bracket */
	if (*copy) {
		strncat(output, copy, len);
	}
}

static int pidf_xml_create_body(struct ast_sip_exten_state_data *data, const char *local,
				const char *remote, struct ast_str **body_text)
{
	pjpidf_pres *pres;
	pjpidf_tuple *tuple;
	pj_str_t entity, note, id, contact, priority;
	char *statestring = NULL, *pidfstate = NULL, *pidfnote = NULL;
	int local_state, size;
	char sanitized[PJSIP_MAX_URL_SIZE];

	RAII_VAR(pj_pool_t *, pool,
		 pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(),
					 "pidf", 1024, 1024), release_pool);

	exten_state_to_str(data->exten_state, &statestring, &pidfstate,
			   &pidfnote, &local_state);

	if (!(pres = pjpidf_create(pool, pj_cstr(&entity, local)))) {
		ast_log(LOG_WARNING, "Unable to create PIDF presence\n");
		return -1;
	}

	add_non_standard(pool, pres, pidfstate);

	if (!pjpidf_pres_add_note(pool, pres, pj_cstr(&note, pidfnote))) {
		ast_log(LOG_WARNING, "Unable to add note to PIDF presence\n");
		return -1;
	}

	if (!(tuple = pjpidf_pres_add_tuple(pool, pres, pj_cstr(&id, data->exten)))) {
		ast_log(LOG_WARNING, "Unable to create PIDF tuple\n");
		return -1;
	}

	sanitize_xml(remote, sanitized, sizeof(sanitized));
	pjpidf_tuple_set_contact(pool, tuple, pj_cstr(&contact, sanitized));
	pjpidf_tuple_set_contact_prio(pool, tuple, pj_cstr(&priority, "1"));
	pjpidf_status_set_basic_open(pjpidf_tuple_get_status(tuple),
			local_state == NOTIFY_OPEN);

	if (!(size = pjpidf_print(pres, ast_str_buffer(*body_text),
				  ast_str_size(*body_text)))) {
		ast_log(LOG_WARNING, "PIDF body text too large\n");
		return -1;
	}
	*(ast_str_buffer(*body_text) + size) = '\0';
	ast_str_update(*body_text);

	return 0;
}

static struct ast_sip_exten_state_provider pidf_xml_provider = {
	.event_name = "presence",
	.type = "application",
	.subtype = "pidf+xml",
	.body_type = "application/pidf+xml",
	.create_body = pidf_xml_create_body
};

static int xpidf_xml_create_body(struct ast_sip_exten_state_data *data, const char *local,
				 const char *remote, struct ast_str **body_text)
{
	static pj_str_t STR_ADDR_PARAM = { ";user=ip", 8 };
	pjxpidf_pres *pres;
	pj_xml_attr *attr;
	pj_str_t name, uri;
	char *statestring = NULL, *pidfstate = NULL, *pidfnote = NULL;
	int local_state, size;
	char sanitized[PJSIP_MAX_URL_SIZE];
	pj_xml_node *atom;
	pj_xml_node *address;
	pj_xml_node *status;
	pj_xml_node *msnsubstatus;

	RAII_VAR(pj_pool_t *, pool,
		 pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(),
					 "pidf", 1024, 1024), release_pool);

	exten_state_to_str(data->exten_state, &statestring, &pidfstate,
			   &pidfnote, &local_state);

	if (!(pres = pjxpidf_create(pool, pj_cstr(&name, local)))) {
		ast_log(LOG_WARNING, "Unable to create PIDF presence\n");
		return -1;
	}

	find_node_attr(pool, pres, "atom", "id", &atom, &attr);
	pj_strdup2(pool, &attr->value, data->exten);

	find_node_attr(pool, atom, "address", "uri", &address, &attr);

	sanitize_xml(remote, sanitized, sizeof(sanitized));

	uri.ptr = (char*) pj_pool_alloc(pool, strlen(sanitized) + STR_ADDR_PARAM.slen);
	pj_strcpy2( &uri, sanitized);

	pj_strcat( &uri, &STR_ADDR_PARAM);
	pj_strdup(pool, &attr->value, &uri);

	create_attr(pool, address, "priority", "0.80000");

	find_node_attr(pool, address, "status", "status", &status, &attr);
	pj_strdup2(pool, &attr->value,
		   (local_state ==  NOTIFY_OPEN) ? "open" :
		   (local_state == NOTIFY_INUSE) ? "inuse" : "closed");

	find_node_attr(pool, address, "msnsubstatus", "substatus", &msnsubstatus, &attr);
	pj_strdup2(pool, &attr->value,
		   (local_state == NOTIFY_OPEN) ? "online" :
		   (local_state == NOTIFY_INUSE) ? "onthephone" : "offline");

	if (!(size = pjxpidf_print(pres, ast_str_buffer(*body_text),
				  ast_str_size(*body_text)))) {
		ast_log(LOG_WARNING, "XPIDF body text too large\n");
		return -1;
	}

	*(ast_str_buffer(*body_text) + size) = '\0';
	ast_str_update(*body_text);

	return 0;
}

static struct ast_sip_exten_state_provider xpidf_xml_provider = {
	.event_name = "presence",
	.type = "application",
	.subtype = "xpidf+xml",
	.body_type = "application/xpidf+xml",
	.create_body = xpidf_xml_create_body
};

static struct ast_sip_exten_state_provider cpim_pidf_xml_provider = {
	.event_name = "presence",
	.type = "application",
	.subtype = "cpim-pidf+xml",
	.body_type = "application/cpim-pidf+xml",
	.create_body = xpidf_xml_create_body,
};

static int load_module(void)
{
	if (ast_sip_register_exten_state_provider(&pidf_xml_provider)) {
		ast_log(LOG_WARNING, "Unable to load provider event_name=%s, body_type=%s",
			pidf_xml_provider.event_name, pidf_xml_provider.body_type);
	}

	if (ast_sip_register_exten_state_provider(&xpidf_xml_provider)) {
		ast_log(LOG_WARNING, "Unable to load provider event_name=%s, body_type=%s",
			xpidf_xml_provider.event_name, xpidf_xml_provider.body_type);
	}

	if (ast_sip_register_exten_state_provider(&cpim_pidf_xml_provider)) {
		ast_log(LOG_WARNING, "Unable to load provider event_name=%s, body_type=%s",
			cpim_pidf_xml_provider.event_name, cpim_pidf_xml_provider.body_type);
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_exten_state_provider(&cpim_pidf_xml_provider);
	ast_sip_unregister_exten_state_provider(&xpidf_xml_provider);
	ast_sip_unregister_exten_state_provider(&pidf_xml_provider);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Extension State PIDF Provider",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
