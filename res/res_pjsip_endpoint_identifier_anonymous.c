/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"

static int get_endpoint_details(pjsip_rx_data *rdata, char *domain, size_t domain_size)
{
	pjsip_uri *from = rdata->msg_info.from->uri;
	pjsip_sip_uri *sip_from;
	if (!PJSIP_URI_SCHEME_IS_SIP(from) && !PJSIP_URI_SCHEME_IS_SIPS(from)) {
		return -1;
	}
	sip_from = (pjsip_sip_uri *) pjsip_uri_get_uri(from);
	ast_copy_pj_str(domain, &sip_from->host, domain_size);
	return 0;
}

static int find_transport_state_in_use(void *obj, void *arg, int flags)
{
	struct ast_sip_transport_state *transport_state = obj;
	pjsip_rx_data *rdata = arg;

	if (transport_state->transport == rdata->tp_info.transport
		|| (transport_state->factory
			&& !pj_strcmp(&transport_state->factory->addr_name.host, &rdata->tp_info.transport->local_name.host)
			&& transport_state->factory->addr_name.port == rdata->tp_info.transport->local_name.port)) {
		return CMP_MATCH;
	}

	return 0;
}

#define DOMAIN_NAME_LEN 255

static struct ast_sip_endpoint *anonymous_identify(pjsip_rx_data *rdata)
{
	char domain_name[DOMAIN_NAME_LEN + 1];
	struct ast_sip_endpoint *endpoint;

	if (get_endpoint_details(rdata, domain_name, sizeof(domain_name))) {
		return NULL;
	}

	if (!ast_sip_get_disable_multi_domain()) {
		struct ast_sip_domain_alias *alias;
		struct ao2_container *transport_states;
		struct ast_sip_transport_state *transport_state = NULL;
		struct ast_sip_transport *transport = NULL;
		char id[sizeof("anonymous@") + DOMAIN_NAME_LEN];

		/* Attempt to find the endpoint given the name and domain provided */
		snprintf(id, sizeof(id), "anonymous@%s", domain_name);
		endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", id);
		if (endpoint) {
			goto done;
		}

		/* See if an alias exists for the domain provided */
		alias = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "domain_alias",
			domain_name);
		if (alias) {
			snprintf(id, sizeof(id), "anonymous@%s", alias->domain);
			ao2_ref(alias, -1);
			endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", id);
			if (endpoint) {
				goto done;
			}
		}

		/* See if the transport this came in on has a provided domain */
		if ((transport_states = ast_sip_get_transport_states())
			&& (transport_state = ao2_callback(transport_states, 0, find_transport_state_in_use, rdata))
			&& (transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport", transport_state->id))
			&& !ast_strlen_zero(transport->domain)) {
			snprintf(id, sizeof(id), "anonymous@%s", transport->domain);
			endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", id);
		}
		ao2_cleanup(transport);
		ao2_cleanup(transport_state);
		ao2_cleanup(transport_states);
		if (endpoint) {
			goto done;
		}
	}

	/* Fall back to no domain */
	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", "anonymous");

done:
	if (endpoint) {
		ast_debug(3, "Retrieved anonymous endpoint '%s'\n", ast_sorcery_object_get_id(endpoint));
	}
	return endpoint;
}

static struct ast_sip_endpoint_identifier anonymous_identifier = {
	.identify_endpoint = anonymous_identify,
};

static int load_module(void)
{
	ast_sip_register_endpoint_identifier_with_name(&anonymous_identifier, "anonymous");
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_endpoint_identifier(&anonymous_identifier);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "PJSIP Anonymous endpoint identifier",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_pjsip",
);
