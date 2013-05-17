/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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

#include "asterisk.h"

#include <pjsip.h>
#include <pjlib.h>

#include "asterisk/res_sip.h"
#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/sorcery.h"
#include "asterisk/acl.h"

static int destroy_transport_state(void *data)
{
	pjsip_transport *transport = data;
	pjsip_transport_shutdown(transport);
	return 0;
}

/*! \brief Destructor for transport state information */
static void transport_state_destroy(void *obj)
{
	struct ast_sip_transport_state *state = obj;

	if (state->transport) {
		ast_sip_push_task_synchronous(NULL, destroy_transport_state, state->transport);
	}
}

/*! \brief Destructor for transport */
static void transport_destroy(void *obj)
{
	struct ast_sip_transport *transport = obj;

	ast_string_field_free_memory(transport);
	ast_free_ha(transport->localnet);

	if (transport->external_address_refresher) {
		ast_dnsmgr_release(transport->external_address_refresher);
	}

	ao2_cleanup(transport->state);
}

/*! \brief Allocator for transport */
static void *transport_alloc(const char *name)
{
	struct ast_sip_transport *transport = ao2_alloc(sizeof(*transport), transport_destroy);

	if (!transport) {
		return NULL;
	}

	if (ast_string_field_init(transport, 256)) {
		ao2_cleanup(transport);
		return NULL;
	}

	pjsip_tls_setting_default(&transport->tls);
	transport->tls.ciphers = transport->ciphers;

	return transport;
}

/*! \brief Apply handler for transports */
static int transport_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ast_sip_transport *transport = obj;
	RAII_VAR(struct ast_sip_transport *, existing, ast_sorcery_retrieve_by_id(sorcery, "transport", ast_sorcery_object_get_id(obj)), ao2_cleanup);
	pj_status_t res = -1;

	if (!existing || !existing->state) {
		if (!(transport->state = ao2_alloc(sizeof(*transport->state), transport_state_destroy))) {
			ast_log(LOG_ERROR, "Transport state for '%s' could not be allocated\n", ast_sorcery_object_get_id(obj));
			return -1;
		}
	} else {
		transport->state = existing->state;
		ao2_ref(transport->state, +1);
	}

	/* Once active a transport can not be reconfigured */
	if (transport->state->transport || transport->state->factory) {
		return -1;
	}

	/* Set default port if not present */
	if (!pj_sockaddr_get_port(&transport->host)) {
		pj_sockaddr_set_port(&transport->host, (transport->type == AST_TRANSPORT_TLS) ? 5061 : 5060);
	}

	/* Now that we know what address family we can set up a dnsmgr refresh for the external media address if present */
	if (!ast_strlen_zero(transport->external_signaling_address)) {
		if (transport->host.addr.sa_family == pj_AF_INET()) {
			transport->external_address.ss.ss_family = AF_INET;
		} else if (transport->host.addr.sa_family == pj_AF_INET6()) {
			transport->external_address.ss.ss_family = AF_INET6;
		} else {
			ast_log(LOG_ERROR, "Unknown address family for transport '%s', could not get external signaling address\n",
					ast_sorcery_object_get_id(obj));
			return -1;
		}

		if (ast_dnsmgr_lookup(transport->external_signaling_address, &transport->external_address, &transport->external_address_refresher, NULL) < 0) {
			ast_log(LOG_ERROR, "Could not create dnsmgr for external signaling address on '%s'\n", ast_sorcery_object_get_id(obj));
			return -1;
		}
	}

	if (transport->type == AST_TRANSPORT_UDP) {
		if (transport->host.addr.sa_family == pj_AF_INET()) {
			res = pjsip_udp_transport_start(ast_sip_get_pjsip_endpoint(), &transport->host.ipv4, NULL, transport->async_operations, &transport->state->transport);
		} else if (transport->host.addr.sa_family == pj_AF_INET6()) {
			res = pjsip_udp_transport_start6(ast_sip_get_pjsip_endpoint(), &transport->host.ipv6, NULL, transport->async_operations, &transport->state->transport);
		}
	} else if (transport->type == AST_TRANSPORT_TCP) {
		pjsip_tcp_transport_cfg cfg;

		pjsip_tcp_transport_cfg_default(&cfg, transport->host.addr.sa_family);
		cfg.bind_addr = transport->host;
		cfg.async_cnt = transport->async_operations;

		res = pjsip_tcp_transport_start3(ast_sip_get_pjsip_endpoint(), &cfg, &transport->state->factory);
	} else if (transport->type == AST_TRANSPORT_TLS) {
		transport->tls.ca_list_file = pj_str((char*)transport->ca_list_file);
		transport->tls.cert_file = pj_str((char*)transport->cert_file);
		transport->tls.privkey_file = pj_str((char*)transport->privkey_file);
		transport->tls.password = pj_str((char*)transport->password);

		res = pjsip_tls_transport_start2(ast_sip_get_pjsip_endpoint(), &transport->tls, &transport->host, NULL, transport->async_operations, &transport->state->factory);
	}

	if (res != PJ_SUCCESS) {
		char msg[PJ_ERR_MSG_SIZE];

		pjsip_strerror(res, msg, sizeof(msg));
		ast_log(LOG_ERROR, "Transport '%s' could not be started: %s\n", ast_sorcery_object_get_id(obj), msg);
		return -1;
	}
	return 0;
}

/*! \brief Custom handler for turning a string protocol into an enum */
static int transport_protocol_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;

	if (!strcasecmp(var->value, "udp")) {
		transport->type = AST_TRANSPORT_UDP;
	} else if (!strcasecmp(var->value, "tcp")) {
		transport->type = AST_TRANSPORT_TCP;
	} else if (!strcasecmp(var->value, "tls")) {
		transport->type = AST_TRANSPORT_TLS;
	} else {
		/* TODO: Implement websockets */
		return -1;
	}

	return 0;
}

/*! \brief Custom handler for turning a string bind into a pj_sockaddr */
static int transport_bind_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	pj_str_t buf;

	return (pj_sockaddr_parse(pj_AF_UNSPEC(), 0, pj_cstr(&buf, var->value), &transport->host) != PJ_SUCCESS) ? -1 : 0;
}

/*! \brief Custom handler for TLS boolean settings */
static int transport_tls_bool_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;

	if (!strcasecmp(var->name, "verify_server")) {
		transport->tls.verify_server = ast_true(var->value) ? PJ_TRUE : PJ_FALSE;
	} else if (!strcasecmp(var->name, "verify_client")) {
		transport->tls.verify_client = ast_true(var->value) ? PJ_TRUE : PJ_FALSE;
	} else if (!strcasecmp(var->name, "require_client_cert")) {
		transport->tls.require_client_cert = ast_true(var->value) ? PJ_TRUE : PJ_FALSE;
	} else {
		return -1;
	}

	return 0;
}

/*! \brief Custom handler for TLS method setting */
static int transport_tls_method_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;

	if (!strcasecmp(var->value, "default")) {
		transport->tls.method = PJSIP_SSL_DEFAULT_METHOD;
	} else if (!strcasecmp(var->value, "unspecified")) {
		transport->tls.method = PJSIP_SSL_UNSPECIFIED_METHOD;
	} else if (!strcasecmp(var->value, "tlsv1")) {
		transport->tls.method = PJSIP_TLSV1_METHOD;
	} else if (!strcasecmp(var->value, "sslv2")) {
		transport->tls.method = PJSIP_SSLV2_METHOD;
	} else if (!strcasecmp(var->value, "sslv3")) {
		transport->tls.method = PJSIP_SSLV3_METHOD;
	} else if (!strcasecmp(var->value, "sslv23")) {
		transport->tls.method = PJSIP_SSLV23_METHOD;
	} else {
		return -1;
	}

	return 0;
}

/*! \brief Custom handler for TLS cipher setting */
static int transport_tls_cipher_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	pj_ssl_cipher cipher;

	if (transport->tls.ciphers_num == (SIP_TLS_MAX_CIPHERS - 1)) {
		return -1;
	}

	/* TODO: Check this over/tweak - it's taken from pjsua for now */
	if (!strnicmp(var->value, "0x", 2)) {
		pj_str_t cipher_st = pj_str((char*)var->value + 2);
		cipher = pj_strtoul2(&cipher_st, NULL, 16);
	} else {
		cipher = atoi(var->value);
	}

	if (pj_ssl_cipher_is_supported(cipher)) {
		transport->ciphers[transport->tls.ciphers_num++] = cipher;
		return 0;
	} else {
		ast_log(LOG_ERROR, "Cipher '%s' is unsupported\n", var->value);
		return -1;
	}
}

/*! \brief Custom handler for localnet setting */
static int transport_localnet_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	int error = 0;

	if (!(transport->localnet = ast_append_ha("d", var->value, transport->localnet, &error))) {
		return -1;
	}

	return error;
}

/*! \brief Initialize sorcery with transport support */
int ast_sip_initialize_sorcery_transport(struct ast_sorcery *sorcery)
{
	ast_sorcery_apply_default(sorcery, "transport", "config", "res_sip.conf,criteria=type=transport");

	if (ast_sorcery_object_register(sorcery, "transport", transport_alloc, NULL, transport_apply)) {
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, "transport", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "protocol", "udp", transport_protocol_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "bind", "", transport_bind_handler, NULL, 0, 0);
	ast_sorcery_object_field_register(sorcery, "transport", "async_operations", "1", OPT_UINT_T, 0, FLDSET(struct ast_sip_transport, async_operations));
	ast_sorcery_object_field_register(sorcery, "transport", "ca_list_file", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, ca_list_file));
	ast_sorcery_object_field_register(sorcery, "transport", "cert_file", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, cert_file));
	ast_sorcery_object_field_register(sorcery, "transport", "privkey_file", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, privkey_file));
	ast_sorcery_object_field_register(sorcery, "transport", "password", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, password));
	ast_sorcery_object_field_register(sorcery, "transport", "external_signaling_address", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, external_signaling_address));
	ast_sorcery_object_field_register(sorcery, "transport", "external_signaling_port", "0", OPT_UINT_T, PARSE_IN_RANGE, FLDSET(struct ast_sip_transport, external_signaling_port), 0, 65535);
	ast_sorcery_object_field_register(sorcery, "transport", "external_media_address", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, external_media_address));
	ast_sorcery_object_field_register(sorcery, "transport", "domain", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, domain));
	ast_sorcery_object_field_register_custom(sorcery, "transport", "verify_server", "", transport_tls_bool_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "verify_client", "", transport_tls_bool_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "require_client_cert", "", transport_tls_bool_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "method", "", transport_tls_method_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "cipher", "", transport_tls_cipher_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "localnet", "", transport_localnet_handler, NULL, 0, 0);

	return 0;
}
