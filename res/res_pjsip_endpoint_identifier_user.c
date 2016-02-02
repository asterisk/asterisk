/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"

static int get_endpoint_details(pjsip_rx_data *rdata, char *endpoint, size_t endpoint_size, char *domain, size_t domain_size)
{
	pjsip_uri *from = rdata->msg_info.from->uri;
	pjsip_sip_uri *sip_from;
	if (!PJSIP_URI_SCHEME_IS_SIP(from) && !PJSIP_URI_SCHEME_IS_SIPS(from)) {
		return -1;
	}
	sip_from = (pjsip_sip_uri *) pjsip_uri_get_uri(from);
	ast_copy_pj_str(endpoint, &sip_from->user, endpoint_size);
	ast_copy_pj_str(domain, &sip_from->host, domain_size);
	return 0;
}

static int find_transport_state_in_use(void *obj, void *arg, int flags)
{
	struct ast_sip_transport_state *transport_state = obj;
	pjsip_rx_data *rdata = arg;

	if (transport_state && ((transport_state->transport == rdata->tp_info.transport) ||
		(transport_state->factory && !pj_strcmp(&transport_state->factory->addr_name.host, &rdata->tp_info.transport->local_name.host) &&
			transport_state->factory->addr_name.port == rdata->tp_info.transport->local_name.port))) {
		return CMP_MATCH | CMP_STOP;
	}

	return 0;
}

static struct ast_sip_endpoint *username_identify(pjsip_rx_data *rdata)
{
	char endpoint_name[64], domain_name[64], id[AST_UUID_STR_LEN];
	struct ast_sip_endpoint *endpoint;
	RAII_VAR(struct ast_sip_domain_alias *, alias, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, transport_states, NULL, ao2_cleanup);
	RAII_VAR(struct ast_sip_transport_state *, transport_state, NULL, ao2_cleanup);
	RAII_VAR(struct ast_sip_transport *, transport, NULL, ao2_cleanup);

	if (get_endpoint_details(rdata, endpoint_name, sizeof(endpoint_name), domain_name, sizeof(domain_name))) {
		return NULL;
	}

	/* Attempt to find the endpoint given the name and domain provided */
	snprintf(id, sizeof(id), "%s@%s", endpoint_name, domain_name);
	if ((endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", id))) {
		goto done;
	}

	/* See if an alias exists for the domain provided */
	if ((alias = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "domain_alias", domain_name))) {
		snprintf(id, sizeof(id), "%s@%s", endpoint_name, alias->domain);
		if ((endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", id))) {
			goto done;
		}
	}

	/* See if the transport this came in on has a provided domain */
	if ((transport_states = ast_sip_get_transport_states())
		&& (transport_state = ao2_callback(transport_states, 0, find_transport_state_in_use, rdata))
		&& (transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport", transport_state->id))
		&& !ast_strlen_zero(transport->domain)) {
		snprintf(id, sizeof(id), "anonymous@%s", transport->domain);
		if ((endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", id))) {
			goto done;
		}
	}

	/* Fall back to no domain */
	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", endpoint_name);

done:
	if (endpoint) {
		if (!(endpoint->ident_method & AST_SIP_ENDPOINT_IDENTIFY_BY_USERNAME)) {
			ao2_ref(endpoint, -1);
			return NULL;
		}
		ast_debug(3, "Retrieved endpoint %s\n", ast_sorcery_object_get_id(endpoint));
	} else {
		ast_debug(3, "Could not identify endpoint by username '%s'\n", endpoint_name);
	}
	return endpoint;
}

static struct ast_sip_endpoint_identifier username_identifier = {
	.identify_endpoint = username_identify,
};

static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	ast_sip_register_endpoint_identifier_with_name(&username_identifier, "username");
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_endpoint_identifier(&username_identifier);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP username endpoint identifier",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
);
