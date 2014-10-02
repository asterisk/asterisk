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

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_cli.h"
#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/sorcery.h"
#include "asterisk/acl.h"
#include "include/res_pjsip_private.h"
#include "asterisk/http_websocket.h"

static int sip_transport_to_ami(const struct ast_sip_transport *transport,
				struct ast_str **buf)
{
	return ast_sip_sorcery_object_to_ami(transport, buf);
}

static int format_ami_endpoint_transport(const struct ast_sip_endpoint *endpoint,
					 struct ast_sip_ami *ami)
{
	RAII_VAR(struct ast_str *, buf, NULL, ast_free);
	RAII_VAR(struct ast_sip_transport *, transport, NULL, ao2_cleanup);

	if (ast_strlen_zero(endpoint->transport)) {
		return 0;
	}

	buf = ast_sip_create_ami_event("TransportDetail", ami);
	if (!buf) {
		return -1;
	}

	transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport",
		endpoint->transport);
	if (!transport) {
		astman_send_error_va(ami->s, ami->m, "Unable to retrieve "
				     "transport %s\n", endpoint->transport);
		return -1;
	}

	sip_transport_to_ami(transport, &buf);

	ast_str_append(&buf, 0, "EndpointName: %s\r\n",
		       ast_sorcery_object_get_id(endpoint));

	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	ami->count++;

	return 0;
}

struct ast_sip_endpoint_formatter endpoint_transport_formatter = {
	.format_ami = format_ami_endpoint_transport
};

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
	struct ast_sip_transport *transport = ast_sorcery_generic_alloc(sizeof(*transport), transport_destroy);

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

static void set_qos(struct ast_sip_transport *transport, pj_qos_params *qos)
{
	int tos_as_dscp = transport->tos >> 2;

	if (transport->tos) {
		qos->flags |= PJ_QOS_PARAM_HAS_DSCP;
		qos->dscp_val = tos_as_dscp;
	}
	if (transport->cos) {
		qos->flags |= PJ_QOS_PARAM_HAS_SO_PRIO;
		qos->so_prio = transport->cos;
	}
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

	if (transport->host.addr.sa_family != PJ_AF_INET && transport->host.addr.sa_family != PJ_AF_INET6) {
		ast_log(LOG_ERROR, "Transport '%s' could not be started as binding not specified\n", ast_sorcery_object_get_id(obj));
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

		if (res == PJ_SUCCESS && (transport->tos || transport->cos)) {
			pj_sock_t sock;
			pj_qos_params qos_params;

			sock = pjsip_udp_transport_get_socket(transport->state->transport);
			pj_sock_get_qos_params(sock, &qos_params);
			set_qos(transport, &qos_params);
			pj_sock_set_qos_params(sock, &qos_params);
		}
	} else if (transport->type == AST_TRANSPORT_TCP) {
		pjsip_tcp_transport_cfg cfg;

		pjsip_tcp_transport_cfg_default(&cfg, transport->host.addr.sa_family);
		cfg.bind_addr = transport->host;
		cfg.async_cnt = transport->async_operations;
		set_qos(transport, &cfg.qos_params);

		res = pjsip_tcp_transport_start3(ast_sip_get_pjsip_endpoint(), &cfg, &transport->state->factory);
	} else if (transport->type == AST_TRANSPORT_TLS) {
		transport->tls.ca_list_file = pj_str((char*)transport->ca_list_file);
		transport->tls.cert_file = pj_str((char*)transport->cert_file);
		transport->tls.privkey_file = pj_str((char*)transport->privkey_file);
		transport->tls.password = pj_str((char*)transport->password);
		set_qos(transport, &transport->tls.qos_params);

		res = pjsip_tls_transport_start2(ast_sip_get_pjsip_endpoint(), &transport->tls, &transport->host, NULL, transport->async_operations, &transport->state->factory);
	} else if ((transport->type == AST_TRANSPORT_WS) || (transport->type == AST_TRANSPORT_WSS)) {
		if (transport->cos || transport->tos) {
			ast_log(LOG_WARNING, "TOS and COS values ignored for websocket transport\n");
		}
		res = PJ_SUCCESS;
	}

	if (res != PJ_SUCCESS) {
		char msg[PJ_ERR_MSG_SIZE];

		pj_strerror(res, msg, sizeof(msg));
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
	} else if (!strcasecmp(var->value, "ws")) {
		transport->type = AST_TRANSPORT_WS;
	} else if (!strcasecmp(var->value, "wss")) {
		transport->type = AST_TRANSPORT_WSS;
	} else {
		return -1;
	}

	return 0;
}

static const char *transport_types[] = {
	[AST_TRANSPORT_UDP] = "udp",
	[AST_TRANSPORT_TCP] = "tcp",
	[AST_TRANSPORT_TLS] = "tls",
	[AST_TRANSPORT_WS] = "ws",
	[AST_TRANSPORT_WSS] = "wss"
};

static int transport_protocol_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;

	if (ARRAY_IN_BOUNDS(transport->type, transport_types)) {
		*buf = ast_strdup(transport_types[transport->type]);
	}

	return 0;
}

/*! \brief Custom handler for turning a string bind into a pj_sockaddr */
static int transport_bind_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	pj_str_t buf;
	int rc = pj_sockaddr_parse(pj_AF_UNSPEC(), 0, pj_cstr(&buf, var->value), &transport->host);

	return rc != PJ_SUCCESS ? -1 : 0;
}

static int transport_bind_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;

	if (!(*buf = ast_calloc(MAX_OBJECT_FIELD, sizeof(char)))) {
		return -1;
	}

	/* include port as well as brackets if IPv6 */
	pj_sockaddr_print(&transport->host, *buf, MAX_OBJECT_FIELD, 1 | 2);

	return 0;
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

static int verify_server_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;
	*buf = ast_strdup(AST_YESNO(transport->tls.verify_server));
	return 0;
}

static int verify_client_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;
	*buf = ast_strdup(AST_YESNO(transport->tls.verify_client));
	return 0;
}

static int require_client_cert_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;
	*buf = ast_strdup(AST_YESNO(transport->tls.require_client_cert));
	return 0;
}

/*! \brief Custom handler for TLS method setting */
static int transport_tls_method_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;

	if (ast_strlen_zero(var->value) || !strcasecmp(var->value, "default")) {
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

static const char *tls_method_map[] = {
	[PJSIP_SSL_DEFAULT_METHOD] = "default",
	[PJSIP_SSL_UNSPECIFIED_METHOD] = "unspecified",
	[PJSIP_TLSV1_METHOD] = "tlsv1",
	[PJSIP_SSLV2_METHOD] = "sslv2",
	[PJSIP_SSLV3_METHOD] = "sslv3",
	[PJSIP_SSLV23_METHOD] = "sslv23",
};

static int tls_method_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;
	if (ARRAY_IN_BOUNDS(transport->tls.method, tls_method_map)) {
		*buf = ast_strdup(tls_method_map[transport->tls.method]);
	}
	return 0;
}

/*! \brief Helper function which turns a cipher name into an identifier */
static pj_ssl_cipher cipher_name_to_id(const char *name)
{
	pj_ssl_cipher ciphers[100];
	pj_ssl_cipher id = 0;
	unsigned int cipher_num = PJ_ARRAY_SIZE(ciphers);
	int pos;
	const char *pos_name;

	if (pj_ssl_cipher_get_availables(ciphers, &cipher_num)) {
		return 0;
	}

	for (pos = 0; pos < cipher_num; ++pos) {
		pos_name = pj_ssl_cipher_name(ciphers[pos]);
		if (!pos_name || strcmp(pos_name, name)) {
			continue;
		}

		id = ciphers[pos];
		break;
	}

	return id;
}

/*!
 * \internal
 * \brief Add a new cipher to the transport's cipher list array.
 *
 * \param transport Which transport to add the cipher to.
 * \param name Cipher identifier name.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int transport_cipher_add(struct ast_sip_transport *transport, const char *name)
{
	pj_ssl_cipher cipher;
	int idx;

	cipher = cipher_name_to_id(name);
	if (!cipher) {
		/* TODO: Check this over/tweak - it's taken from pjsua for now */
		if (!strnicmp(name, "0x", 2)) {
			pj_str_t cipher_st = pj_str((char *) name + 2);
			cipher = pj_strtoul2(&cipher_st, NULL, 16);
		} else {
			cipher = atoi(name);
		}
	}

	if (pj_ssl_cipher_is_supported(cipher)) {
		for (idx = transport->tls.ciphers_num; idx--;) {
			if (transport->ciphers[idx] == cipher) {
				/* The cipher is already in the list. */
				return 0;
			}
		}
		transport->ciphers[transport->tls.ciphers_num++] = cipher;
		return 0;
	} else {
		ast_log(LOG_ERROR, "Cipher '%s' is unsupported\n", name);
		return -1;
	}
}

/*! \brief Custom handler for TLS cipher setting */
static int transport_tls_cipher_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	char *parse;
	char *name;
	int res = 0;

	parse = ast_strdupa(S_OR(var->value, ""));
	while ((name = strsep(&parse, ","))) {
		name = ast_strip(name);
		if (ast_strlen_zero(name)) {
			continue;
		}
		if (ARRAY_LEN(transport->ciphers) <= transport->tls.ciphers_num) {
			ast_log(LOG_ERROR, "Too many ciphers specified\n");
			res = -1;
			break;
		}
		res |= transport_cipher_add(transport, name);
	}
	return res ? -1 : 0;
}

static void cipher_to_str(char **buf, const pj_ssl_cipher *ciphers, unsigned int cipher_num)
{
	struct ast_str *str;
	int idx;

	str = ast_str_create(128);
	if (!str) {
		*buf = NULL;
		return;
	}

	for (idx = 0; idx < cipher_num; ++idx) {
		ast_str_append(&str, 0, "%s", pj_ssl_cipher_name(ciphers[idx]));
		if (idx < cipher_num - 1) {
			ast_str_append(&str, 0, ", ");
		}
	}

	*buf = ast_strdup(ast_str_buffer(str));
	ast_free(str);
}

static int transport_tls_cipher_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;

	cipher_to_str(buf, transport->ciphers, transport->tls.ciphers_num);
	return *buf ? 0 : -1;
}

static char *handle_pjsip_list_ciphers(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	pj_ssl_cipher ciphers[100];
	unsigned int cipher_num = PJ_ARRAY_SIZE(ciphers);
	char *buf;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip list ciphers";
		e->usage = "Usage: pjsip list ciphers\n"
			"       List available OpenSSL cipher names.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (pj_ssl_cipher_get_availables(ciphers, &cipher_num) || !cipher_num) {
		buf = NULL;
	} else {
		cipher_to_str(&buf, ciphers, cipher_num);
	}

	if (!ast_strlen_zero(buf)) {
		ast_cli(a->fd, "Available ciphers: '%s'\n", buf);
	} else {
		ast_cli(a->fd, "No available ciphers\n");
	}
	ast_free(buf);
	return CLI_SUCCESS;
}

/*! \brief Custom handler for localnet setting */
static int transport_localnet_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	int error = 0;

	if (ast_strlen_zero(var->value)) {
		ast_free_ha(transport->localnet);
		transport->localnet = NULL;
		return 0;
	}

	if (!(transport->localnet = ast_append_ha("d", var->value, transport->localnet, &error))) {
		return -1;
	}

	return error;
}

static int localnet_to_vl(const void *obj, struct ast_variable **fields)
{
	const struct ast_sip_transport *transport = obj;

	char str[MAX_OBJECT_FIELD];
	struct ast_variable *head = NULL;
	struct ast_ha *ha = transport->localnet;

	for (; ha; ha = ha->next) {
		const char *addr = ast_strdupa(ast_sockaddr_stringify_addr(&ha->addr));
		snprintf(str, MAX_OBJECT_FIELD, "%s%s/%s", ha->sense == AST_SENSE_ALLOW ? "!" : "",
			addr, ast_sockaddr_stringify_addr(&ha->netmask));

		ast_variable_list_append(&head, ast_variable_new("local_net", str, ""));
	}

	if (head) {
		*fields = head;
	}

	return 0;
}

static int localnet_to_str(const void *obj, const intptr_t *args, char **buf)
{
	RAII_VAR(struct ast_str *, str, ast_str_create(MAX_OBJECT_FIELD), ast_free);
	const struct ast_sip_transport *transport = obj;

	ast_ha_join(transport->localnet, &str);
	*buf = ast_strdup(ast_str_buffer(str));
	return 0;
}

/*! \brief Custom handler for TOS setting */
static int transport_tos_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	unsigned int value;

	if (ast_str2tos(var->value, &value)) {
		ast_log(LOG_ERROR, "Error configuring transport '%s' - Could not "
			"interpret 'tos' value '%s'\n",
			ast_sorcery_object_get_id(transport), var->value);
		return -1;
	}

	if (value % 4) {
		value = value >> 2;
		value = value << 2;
		ast_log(LOG_WARNING,
			"transport '%s' - 'tos' value '%s' uses bits that are "
			"discarded when converted to DSCP. Using equivalent %u instead.\n",
			ast_sorcery_object_get_id(transport), var->value, value);
	}

	transport->tos = value;
	return 0;
}

static int tos_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;

	if (ast_asprintf(buf, "%u", transport->tos) == -1) {
		return -1;
	}
	return 0;
}

static struct ao2_container *cli_get_container(void)
{
	RAII_VAR(struct ao2_container *, container, NULL, ao2_cleanup);
	struct ao2_container *s_container;

	container = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "transport",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!container) {
		return NULL;
	}

	s_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sorcery_object_id_sort, ast_sorcery_object_id_compare);
	if (!s_container) {
		return NULL;
	}

	if (ao2_container_dup(s_container, container, 0)) {
		ao2_ref(s_container, -1);
		return NULL;
	}

	return s_container;
}

static int cli_iterate(void *container, ao2_callback_fn callback, void *args)
{
	const struct ast_sip_endpoint *endpoint = container;
	struct ast_sip_transport *transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(),
		"transport", endpoint->transport);

	if (!transport) {
		return -1;
	}

	return callback(transport, args, 0);
}

static void *cli_retrieve_by_id(const char *id)
{
	return ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport", id);
}

static int cli_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_MAX_WIDTH - indent - 61;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		"%*s:  <TransportId........>  <Type>  <cos>  <tos>  <BindAddress%*.*s>\n",
		indent, "Transport", filler, filler, CLI_HEADER_FILLER);

	return 0;
}

static int cli_print_body(void *obj, void *arg, int flags)
{
	struct ast_sip_transport *transport = obj;
	struct ast_sip_cli_context *context = arg;
	char hoststr[PJ_INET6_ADDRSTRLEN];

	ast_assert(context->output_buffer != NULL);

	pj_sockaddr_print(&transport->host, hoststr, sizeof(hoststr), 3);

	ast_str_append(&context->output_buffer, 0, "%*s:  %-21s  %6s  %5u  %5u  %s\n",
		CLI_INDENT_TO_SPACES(context->indent_level), "Transport",
		ast_sorcery_object_get_id(transport),
		ARRAY_IN_BOUNDS(transport->type, transport_types) ? transport_types[transport->type] : "Unknown",
		transport->cos, transport->tos, hoststr);

	if (context->show_details
		|| (context->show_details_only_level_0 && context->indent_level == 0)) {
		ast_str_append(&context->output_buffer, 0, "\n");
		ast_sip_cli_print_sorcery_objectset(transport, context, 0);
	}

	return 0;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(handle_pjsip_list_ciphers, "List available OpenSSL cipher names"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "List PJSIP Transports",
		.command = "pjsip list transports",
		.usage = "Usage: pjsip list transports\n"
				 "       List the configured PJSIP Transports\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Transports",
		.command = "pjsip show transports",
		.usage = "Usage: pjsip show transports\n"
				 "       Show the configured PJSIP Transport\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Transport",
		.command = "pjsip show transport",
		.usage = "Usage: pjsip show transport <id>\n"
				 "       Show the configured PJSIP Transport\n"),
};

static struct ast_sip_cli_formatter_entry *cli_formatter;

/*! \brief Initialize sorcery with transport support */
int ast_sip_initialize_sorcery_transport(void)
{
	struct ast_sorcery *sorcery = ast_sip_get_sorcery();

	ast_sorcery_apply_default(sorcery, "transport", "config", "pjsip.conf,criteria=type=transport");

	if (ast_sorcery_object_register_no_reload(sorcery, "transport", transport_alloc, NULL, transport_apply)) {
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, "transport", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "protocol", "udp", transport_protocol_handler, transport_protocol_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "bind", "", transport_bind_handler, transport_bind_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sorcery, "transport", "async_operations", "1", OPT_UINT_T, 0, FLDSET(struct ast_sip_transport, async_operations));
	ast_sorcery_object_field_register(sorcery, "transport", "ca_list_file", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, ca_list_file));
	ast_sorcery_object_field_register(sorcery, "transport", "cert_file", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, cert_file));
	ast_sorcery_object_field_register(sorcery, "transport", "priv_key_file", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, privkey_file));
	ast_sorcery_object_field_register(sorcery, "transport", "password", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, password));
	ast_sorcery_object_field_register(sorcery, "transport", "external_signaling_address", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, external_signaling_address));
	ast_sorcery_object_field_register(sorcery, "transport", "external_signaling_port", "0", OPT_UINT_T, PARSE_IN_RANGE, FLDSET(struct ast_sip_transport, external_signaling_port), 0, 65535);
	ast_sorcery_object_field_register(sorcery, "transport", "external_media_address", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, external_media_address));
	ast_sorcery_object_field_register(sorcery, "transport", "domain", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, domain));
	ast_sorcery_object_field_register_custom(sorcery, "transport", "verify_server", "", transport_tls_bool_handler, verify_server_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "verify_client", "", transport_tls_bool_handler, verify_client_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "require_client_cert", "", transport_tls_bool_handler, require_client_cert_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "method", "", transport_tls_method_handler, tls_method_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "cipher", "", transport_tls_cipher_handler, transport_tls_cipher_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "local_net", "", transport_localnet_handler, localnet_to_str, localnet_to_vl, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "tos", "0", transport_tos_handler, tos_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sorcery, "transport", "cos", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_transport, cos));
	ast_sorcery_object_field_register(sorcery, "transport", "websocket_write_timeout", AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT_STR, OPT_INT_T, PARSE_IN_RANGE, FLDSET(struct ast_sip_transport, write_timeout), 1, INT_MAX);

	ast_sip_register_endpoint_formatter(&endpoint_transport_formatter);

	cli_formatter = ao2_alloc(sizeof(struct ast_sip_cli_formatter_entry), NULL);
	if (!cli_formatter) {
		ast_log(LOG_ERROR, "Unable to allocate memory for cli formatter\n");
		return -1;
	}
	cli_formatter->name = "transport";
	cli_formatter->print_header = cli_print_header;
	cli_formatter->print_body = cli_print_body;
	cli_formatter->get_container = cli_get_container;
	cli_formatter->iterate = cli_iterate;
	cli_formatter->get_id = ast_sorcery_object_get_id;
	cli_formatter->retrieve_by_id = cli_retrieve_by_id;

	ast_sip_register_cli_formatter(cli_formatter);
	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));

	return 0;
}

int ast_sip_destroy_sorcery_transport(void)
{
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));
	ast_sip_unregister_cli_formatter(cli_formatter);

	return 0;
}
