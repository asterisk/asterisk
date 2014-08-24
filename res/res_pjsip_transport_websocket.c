/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jason Parker <jparker@digium.com>
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

/*!
 * \brief WebSocket transport module
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_http_websocket</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/module.h"
#include "asterisk/http_websocket.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/taskprocessor.h"

static int transport_type_ws;
static int transport_type_wss;

/*!
 * \brief Wrapper for pjsip_transport, for storing the WebSocket session
 */
struct ws_transport {
	pjsip_transport transport;
	pjsip_rx_data rdata;
	struct ast_websocket *ws_session;
};

/*!
 * \brief Send a message over the WebSocket connection.
 *
 * Called by pjsip transport manager.
 */
static pj_status_t ws_send_msg(pjsip_transport *transport,
                            pjsip_tx_data *tdata,
                            const pj_sockaddr_t *rem_addr,
                            int addr_len,
                            void *token,
                            pjsip_transport_callback callback)
{
	struct ws_transport *wstransport = (struct ws_transport *)transport;

	if (ast_websocket_write(wstransport->ws_session, AST_WEBSOCKET_OPCODE_TEXT, tdata->buf.start, (int)(tdata->buf.cur - tdata->buf.start))) {
		return PJ_EUNKNOWN;
	}

	return PJ_SUCCESS;
}

/*!
 * \brief Destroy the pjsip transport.
 *
 * Called by pjsip transport manager.
 */
static pj_status_t ws_destroy(pjsip_transport *transport)
{
	struct ws_transport *wstransport = (struct ws_transport *)transport;

	if (wstransport->transport.ref_cnt) {
		pj_atomic_destroy(wstransport->transport.ref_cnt);
	}

	if (wstransport->transport.lock) {
		pj_lock_destroy(wstransport->transport.lock);
	}

	pjsip_endpt_release_pool(wstransport->transport.endpt, wstransport->transport.pool);

	return PJ_SUCCESS;
}

static int transport_shutdown(void *data)
{
	pjsip_transport *transport = data;

	pjsip_transport_shutdown(transport);
	return 0;
}

struct transport_create_data {
	struct ws_transport *transport;
	struct ast_websocket *ws_session;
};

/*!
 * \brief Create a pjsip transport.
 */
static int transport_create(void *data)
{
	struct transport_create_data *create_data = data;
	struct ws_transport *newtransport;

	pjsip_endpoint *endpt = ast_sip_get_pjsip_endpoint();
	struct pjsip_tpmgr *tpmgr = pjsip_endpt_get_tpmgr(endpt);

	pj_pool_t *pool;

	pj_str_t buf;

	if (!(pool = pjsip_endpt_create_pool(endpt, "ws", 512, 512))) {
		ast_log(LOG_ERROR, "Failed to allocate WebSocket endpoint pool.\n");
		return -1;
	}

	if (!(newtransport = PJ_POOL_ZALLOC_T(pool, struct ws_transport))) {
		ast_log(LOG_ERROR, "Failed to allocate WebSocket transport.\n");
		pjsip_endpt_release_pool(endpt, pool);
		return -1;
	}

	newtransport->ws_session = create_data->ws_session;

	pj_atomic_create(pool, 0, &newtransport->transport.ref_cnt);
	pj_lock_create_recursive_mutex(pool, pool->obj_name, &newtransport->transport.lock);

	newtransport->transport.pool = pool;
	pj_sockaddr_parse(pj_AF_UNSPEC(), 0, pj_cstr(&buf, ast_sockaddr_stringify(ast_websocket_remote_address(newtransport->ws_session))), &newtransport->transport.key.rem_addr);
	newtransport->transport.key.rem_addr.addr.sa_family = pj_AF_INET();
	newtransport->transport.key.type = ast_websocket_is_secure(newtransport->ws_session) ? transport_type_wss : transport_type_ws;

	newtransport->transport.addr_len = pj_sockaddr_get_len(&newtransport->transport.key.rem_addr);

	pj_sockaddr_cp(&newtransport->transport.local_addr, &newtransport->transport.key.rem_addr);

	newtransport->transport.local_name.host.ptr = (char *)pj_pool_alloc(pool, newtransport->transport.addr_len+4);
	pj_sockaddr_print(&newtransport->transport.key.rem_addr, newtransport->transport.local_name.host.ptr, newtransport->transport.addr_len+4, 0);
	newtransport->transport.local_name.host.slen = pj_ansi_strlen(newtransport->transport.local_name.host.ptr);
	newtransport->transport.local_name.port = pj_sockaddr_get_port(&newtransport->transport.key.rem_addr);

	newtransport->transport.type_name = (char *)pjsip_transport_get_type_name(newtransport->transport.key.type);
	newtransport->transport.flag = pjsip_transport_get_flag_from_type((pjsip_transport_type_e)newtransport->transport.key.type);
	newtransport->transport.info = (char *)pj_pool_alloc(newtransport->transport.pool, 64);

	newtransport->transport.endpt = endpt;
	newtransport->transport.tpmgr = tpmgr;
	newtransport->transport.send_msg = &ws_send_msg;
	newtransport->transport.destroy = &ws_destroy;

	pjsip_transport_register(newtransport->transport.tpmgr, (pjsip_transport *)newtransport);

	create_data->transport = newtransport;
	return 0;
}

struct transport_read_data {
	struct ws_transport *transport;
	char *payload;
	uint64_t payload_len;
};

/*!
 * \brief Pass WebSocket data into pjsip transport manager.
 */
static int transport_read(void *data)
{
	struct transport_read_data *read_data = data;
	struct ws_transport *newtransport = read_data->transport;
	struct ast_websocket *session = newtransport->ws_session;

	pjsip_rx_data *rdata = &newtransport->rdata;
	int recvd;
	pj_str_t buf;

	rdata->tp_info.pool = newtransport->transport.pool;
	rdata->tp_info.transport = &newtransport->transport;

	pj_gettimeofday(&rdata->pkt_info.timestamp);

	pj_memcpy(rdata->pkt_info.packet, read_data->payload, sizeof(rdata->pkt_info.packet));
	rdata->pkt_info.len = read_data->payload_len;
	rdata->pkt_info.zero = 0;

	pj_sockaddr_parse(pj_AF_UNSPEC(), 0, pj_cstr(&buf, ast_sockaddr_stringify(ast_websocket_remote_address(session))), &rdata->pkt_info.src_addr);
	rdata->pkt_info.src_addr.addr.sa_family = pj_AF_INET();

	rdata->pkt_info.src_addr_len = sizeof(rdata->pkt_info.src_addr);

	pj_ansi_strcpy(rdata->pkt_info.src_name, ast_sockaddr_stringify_host(ast_websocket_remote_address(session)));
	rdata->pkt_info.src_port = ast_sockaddr_port(ast_websocket_remote_address(session));

	recvd = pjsip_tpmgr_receive_packet(rdata->tp_info.transport->tpmgr, rdata);

	return (read_data->payload_len == recvd) ? 0 : -1;
}

static int get_write_timeout(void)
{
	int write_timeout = -1;
	struct ao2_container *transports;

	transports = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "transport", AST_RETRIEVE_FLAG_ALL, NULL);

	if (transports) {
		struct ao2_iterator it_transports = ao2_iterator_init(transports, 0);
		struct ast_sip_transport *transport;

		for (; (transport = ao2_iterator_next(&it_transports)); ao2_cleanup(transport)) {
			if (transport->type != AST_TRANSPORT_WS && transport->type != AST_TRANSPORT_WSS) {
				continue;
			}
			ast_debug(5, "Found %s transport with write timeout: %d\n",
				transport->type == AST_TRANSPORT_WS ? "WS" : "WSS",
				transport->write_timeout);
			write_timeout = MAX(write_timeout, transport->write_timeout);
		}
		ao2_cleanup(transports);
	}

	if (write_timeout < 0) {
		write_timeout = AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT;
	}

	ast_debug(1, "Write timeout for WS/WSS transports: %d\n", write_timeout);
	return write_timeout;
}

/*!
 \brief WebSocket connection handler.
 */
static void websocket_cb(struct ast_websocket *session, struct ast_variable *parameters, struct ast_variable *headers)
{
	struct ast_taskprocessor *serializer = NULL;
	struct transport_create_data create_data;
	struct ws_transport *transport = NULL;
	struct transport_read_data read_data;

	if (ast_websocket_set_nonblock(session)) {
		ast_websocket_unref(session);
		return;
	}

	if (ast_websocket_set_timeout(session, get_write_timeout())) {
		ast_websocket_unref(session);
		return;
	}

	if (!(serializer = ast_sip_create_serializer())) {
		ast_websocket_unref(session);
		return;
	}

	create_data.ws_session = session;

	if (ast_sip_push_task_synchronous(serializer, transport_create, &create_data)) {
		ast_log(LOG_ERROR, "Could not create WebSocket transport.\n");
		ast_websocket_unref(session);
		return;
	}

	transport = create_data.transport;
	read_data.transport = transport;

	while (ast_wait_for_input(ast_websocket_fd(session), -1) > 0) {
		enum ast_websocket_opcode opcode;
		int fragmented;

		if (ast_websocket_read(session, &read_data.payload, &read_data.payload_len, &opcode, &fragmented)) {
			break;
		}

		if (opcode == AST_WEBSOCKET_OPCODE_TEXT || opcode == AST_WEBSOCKET_OPCODE_BINARY) {
			ast_sip_push_task_synchronous(serializer, transport_read, &read_data);
		} else if (opcode == AST_WEBSOCKET_OPCODE_CLOSE) {
			break;
		}
	}

	ast_sip_push_task_synchronous(serializer, transport_shutdown, transport);

	ast_taskprocessor_unreference(serializer);
	ast_websocket_unref(session);
}

/*!
 * \brief Store the transport a message came in on, so it can be used for outbound messages to that contact.
 */
static pj_bool_t websocket_on_rx_msg(pjsip_rx_data *rdata)
{
	static const pj_str_t STR_WS = { "ws", 2 };
	static const pj_str_t STR_WSS = { "wss", 3 };
	pjsip_contact_hdr *contact;

	long type = rdata->tp_info.transport->key.type;

	if (type != (long)transport_type_ws && type != (long)transport_type_wss) {
		return PJ_FALSE;
	}

	if ((contact = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, NULL)) &&
		(PJSIP_URI_SCHEME_IS_SIP(contact->uri) || PJSIP_URI_SCHEME_IS_SIPS(contact->uri))) {
		pjsip_sip_uri *uri = pjsip_uri_get_uri(contact->uri);

		pj_cstr(&uri->host, rdata->pkt_info.src_name);
		uri->port = rdata->pkt_info.src_port;
		pj_strdup(rdata->tp_info.pool, &uri->transport_param, (type == (long)transport_type_ws) ? &STR_WS : &STR_WSS);
	}

	rdata->msg_info.via->rport_param = 0;

	return PJ_FALSE;
}

static pjsip_module websocket_module = {
	.name = { "WebSocket Transport Module", 26 },
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_TRANSPORT_LAYER,
	.on_rx_request = websocket_on_rx_msg,
};

static int load_module(void)
{
	pjsip_transport_register_type(PJSIP_TRANSPORT_RELIABLE, "WS", 5060, &transport_type_ws);
	pjsip_transport_register_type(PJSIP_TRANSPORT_RELIABLE, "WSS", 5060, &transport_type_wss);

	if (ast_sip_register_service(&websocket_module) != PJ_SUCCESS) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_websocket_add_protocol("sip", websocket_cb)) {
		ast_sip_unregister_service(&websocket_module);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_service(&websocket_module);
	ast_websocket_remove_protocol("sip", websocket_cb);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP WebSocket Transport Support",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	   );
