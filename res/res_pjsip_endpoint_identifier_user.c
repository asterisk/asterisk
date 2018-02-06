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

static int get_from_header(pjsip_rx_data *rdata, char *username, size_t username_size, char *domain, size_t domain_size)
{
	pjsip_uri *from = rdata->msg_info.from->uri;
	pjsip_sip_uri *sip_from;

	if (!PJSIP_URI_SCHEME_IS_SIP(from) && !PJSIP_URI_SCHEME_IS_SIPS(from)) {
		return -1;
	}
	sip_from = (pjsip_sip_uri *) pjsip_uri_get_uri(from);
	ast_copy_pj_str(username, &sip_from->user, username_size);
	ast_copy_pj_str(domain, &sip_from->host, domain_size);
	return 0;
}

static pjsip_authorization_hdr *get_auth_header(pjsip_rx_data *rdata, char *username,
	size_t username_size, char *realm, size_t realm_size, pjsip_authorization_hdr *start)
{
	pjsip_authorization_hdr *header;

	header = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_AUTHORIZATION, start);

	if (!header || pj_stricmp2(&header->scheme, "digest")) {
		return NULL;
	}

	ast_copy_pj_str(username, &header->credential.digest.username, username_size);
	ast_copy_pj_str(realm, &header->credential.digest.realm, realm_size);

	return header;
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
#define USERNAME_LEN    255

static struct ast_sip_endpoint *find_endpoint(pjsip_rx_data *rdata, char *endpoint_name,
	char *domain_name)
{
	struct ast_sip_endpoint *endpoint;

	if (!ast_sip_get_disable_multi_domain()) {
		struct ast_sip_domain_alias *alias;
		struct ao2_container *transport_states;
		struct ast_sip_transport_state *transport_state = NULL;
		struct ast_sip_transport *transport = NULL;
		char id[DOMAIN_NAME_LEN + USERNAME_LEN + sizeof("@")];

		/* Attempt to find the endpoint given the name and domain provided */
		snprintf(id, sizeof(id), "%s@%s", endpoint_name, domain_name);
		endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", id);
		if (endpoint) {
			return endpoint;
		}

		/* See if an alias exists for the domain provided */
		alias = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "domain_alias",
			domain_name);
		if (alias) {
			snprintf(id, sizeof(id), "%s@%s", endpoint_name, alias->domain);
			ao2_ref(alias, -1);
			endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", id);
			if (endpoint) {
				return endpoint;
			}
		}

		/* See if the transport this came in on has a provided domain */
		if ((transport_states = ast_sip_get_transport_states())
			&& (transport_state = ao2_callback(transport_states, 0, find_transport_state_in_use, rdata))
			&& (transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport", transport_state->id))
			&& !ast_strlen_zero(transport->domain)) {
			snprintf(id, sizeof(id), "%s@%s", endpoint_name, transport->domain);
			endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", id);
		}
		ao2_cleanup(transport);
		ao2_cleanup(transport_state);
		ao2_cleanup(transport_states);
		if (endpoint) {
			return endpoint;
		}
	}

	/* Fall back to no domain */
	return ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", endpoint_name);
}

static struct ast_sip_endpoint *username_identify(pjsip_rx_data *rdata)
{
	char username[USERNAME_LEN + 1];
	char domain[DOMAIN_NAME_LEN + 1];
	struct ast_sip_endpoint *endpoint;

	if (get_from_header(rdata, username, sizeof(username), domain, sizeof(domain))) {
		return NULL;
	}

	/*
	 * We may want to be matched without any user options getting
	 * in the way.
	 */
	AST_SIP_USER_OPTIONS_TRUNCATE_CHECK(username);

	ast_debug(3, "Attempting identify by From username '%s' domain '%s'\n", username, domain);

	endpoint = find_endpoint(rdata, username, domain);
	if (!endpoint) {
		ast_debug(3, "Endpoint not found for From username '%s' domain '%s'\n", username, domain);
		return NULL;
	}
	if (!(endpoint->ident_method & AST_SIP_ENDPOINT_IDENTIFY_BY_USERNAME)) {
		ast_debug(3, "Endpoint found for '%s' but 'username' method not supported'\n", username);
		ao2_cleanup(endpoint);
		return NULL;
	}
	ast_debug(3, "Identified by From username '%s' domain '%s'\n", username, domain);

	return endpoint;
}

static struct ast_sip_endpoint *auth_username_identify(pjsip_rx_data *rdata)
{
	char username[USERNAME_LEN + 1], realm[DOMAIN_NAME_LEN + 1];
	struct ast_sip_endpoint *endpoint;
	pjsip_authorization_hdr *auth_header = NULL;

	while ((auth_header = get_auth_header(rdata, username, sizeof(username), realm, sizeof(realm),
		auth_header ? auth_header->next : NULL))) {
		ast_debug(3, "Attempting identify by Authorization username '%s' realm '%s'\n", username,
			realm);

		endpoint = find_endpoint(rdata, username, realm);
		if (!endpoint) {
			ast_debug(3, "Endpoint not found for Authentication username '%s' realm '%s'\n",
				username, realm);
			ao2_cleanup(endpoint);
			continue;
		}
		if (!(endpoint->ident_method & AST_SIP_ENDPOINT_IDENTIFY_BY_AUTH_USERNAME)) {
			ast_debug(3, "Endpoint found for '%s' but 'auth_username' method not supported'\n",
				username);
			ao2_cleanup(endpoint);
			continue;
		}
		ast_debug(3, "Identified by Authorization username '%s' realm '%s'\n", username, realm);

		return endpoint;
	}

	return NULL;
}


static struct ast_sip_endpoint_identifier username_identifier = {
	.identify_endpoint = username_identify,
};

static struct ast_sip_endpoint_identifier auth_username_identifier = {
	.identify_endpoint = auth_username_identify,
};


static int load_module(void)
{
	ast_sip_register_endpoint_identifier_with_name(&username_identifier, "username");
	ast_sip_register_endpoint_identifier_with_name(&auth_username_identifier, "auth_username");
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_endpoint_identifier(&auth_username_identifier);
	ast_sip_unregister_endpoint_identifier(&username_identifier);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP username endpoint identifier",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 4,
	.requires = "res_pjsip",
);
