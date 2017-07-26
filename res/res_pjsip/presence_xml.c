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

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjlib.h>

#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/res_pjsip_presence_xml.h"

void ast_sip_sanitize_xml(const char *input, char *output, size_t len)
{
	char *copy = ast_strdupa(input);
	char *break_point;
	size_t remaining = len - 1;

	output[0] = '\0';

	while ((break_point = strpbrk(copy, "<>\"&'\n\r")) && remaining) {
		char to_escape = *break_point;

		*break_point = '\0';
		strncat(output, copy, remaining);

		/* The strncat function will write remaining+1 if the string length is
		 * equal to or greater than the size provided to it. We take this into
		 * account by subtracting 1, which ensures that the NULL byte is written
		 * inside of the provided buffer.
		 */
		remaining = len - strlen(output) - 1;

		switch (to_escape) {
		case '<':
			strncat(output, "&lt;", remaining);
			break;
		case '>':
			strncat(output, "&gt;", remaining);
			break;
		case '"':
			strncat(output, "&quot;", remaining);
			break;
		case '&':
			strncat(output, "&amp;", remaining);
			break;
		case '\'':
			strncat(output, "&apos;", remaining);
			break;
		case '\r':
			strncat(output, "&#13;", remaining);
			break;
		case '\n':
			strncat(output, "&#10;", remaining);
			break;
		};

		copy = break_point + 1;
		remaining = len - strlen(output) - 1;
	}

	/* Be sure to copy everything after the final bracket */
	if (*copy && remaining) {
		strncat(output, copy, remaining);
	}
}

void ast_sip_presence_exten_state_to_str(int state, char **statestring, char **pidfstate,
			       char **pidfnote, enum ast_sip_pidf_state *local_state,
			       unsigned int notify_early_inuse_ringing)
{
	switch (state) {
	case AST_EXTENSION_RINGING:
		*statestring = "early";
		*local_state = NOTIFY_INUSE;
		*pidfstate = "on-the-phone";
		*pidfnote = "Ringing";
		break;
	case (AST_EXTENSION_INUSE | AST_EXTENSION_RINGING):
		if (notify_early_inuse_ringing) {
			*statestring = "early";
		} else {
			*statestring = "confirmed";
		}
		*local_state = NOTIFY_INUSE;
		*pidfstate = "on-the-phone";
		*pidfnote = "Ringing";
		break;
	case AST_EXTENSION_INUSE:
		*statestring = "confirmed";
		*local_state = NOTIFY_INUSE;
		*pidfstate = "on-the-phone";
		*pidfnote = "On the phone";
		break;
	case AST_EXTENSION_BUSY:
		*statestring = "confirmed";
		*local_state = NOTIFY_INUSE;
		*pidfstate = "on-the-phone";
		*pidfnote = "On the phone";
		break;
	case AST_EXTENSION_UNAVAILABLE:
		*statestring = "terminated";
		*local_state = NOTIFY_CLOSED;
		*pidfstate = "--";
		*pidfnote = "Unavailable";
		break;
	case AST_EXTENSION_ONHOLD:
		*statestring = "confirmed";
		*local_state = NOTIFY_INUSE;
		*pidfstate = "on-the-phone";
		*pidfnote = "On hold";
		break;
	case AST_EXTENSION_NOT_INUSE:
	default:
		/* Default setting */
		*statestring = "terminated";
		*local_state = NOTIFY_OPEN;
		*pidfstate = "--";
		*pidfnote = "Ready";
		break;
	}
}

pj_xml_attr *ast_sip_presence_xml_create_attr(pj_pool_t *pool,
		pj_xml_node *node, const char *name, const char *value)
{
	pj_xml_attr *attr = PJ_POOL_ALLOC_T(pool, pj_xml_attr);

	pj_strdup2(pool, &attr->name, name);
	pj_strdup2(pool, &attr->value, value);

	pj_xml_add_attr(node, attr);
	return attr;
}

pj_xml_node *ast_sip_presence_xml_create_node(pj_pool_t *pool,
		pj_xml_node *parent, const char* name)
{
	pj_xml_node *node = PJ_POOL_ALLOC_T(pool, pj_xml_node);

	pj_list_init(&node->attr_head);
	pj_list_init(&node->node_head);

	pj_strdup2(pool, &node->name, name);

	node->content.ptr = NULL;
	node->content.slen = 0;

	if (parent) {
		pj_xml_add_node(parent, node);
	}

	return node;
}

void ast_sip_presence_xml_find_node_attr(pj_pool_t* pool,
		pj_xml_node *parent, const char *node_name, const char *attr_name,
		pj_xml_node **node, pj_xml_attr **attr)
{
	pj_str_t name;

	if (!(*node = pj_xml_find_node(parent, pj_cstr(&name, node_name)))) {
		*node = ast_sip_presence_xml_create_node(pool, parent, node_name);
	}

	if (!(*attr = pj_xml_find_attr(*node, pj_cstr(&name, attr_name), NULL))) {
		*attr = ast_sip_presence_xml_create_attr(pool, *node, attr_name, "");
	}
}
