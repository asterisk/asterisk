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
	uint64_t len = tdata->buf.cur - tdata->buf.start;

	if (ast_websocket_write(wstransport->ws_session, AST_WEBSOCKET_OPCODE_TEXT, tdata->buf.start, len)) {
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
	int fd = ast_websocket_fd(wstransport->ws_session);

	if (fd > 0) {
		ast_websocket_close(wstransport->ws_session, 1000);
		shutdown(fd, SHUT_RDWR);
	}

	ao2_ref(wstransport, -1);

	return PJ_SUCCESS;
}

static void transport_dtor(void *arg)
{
	struct ws_transport *wstransport = arg;

	if (wstransport->ws_session) {
		ast_websocket_unref(wstransport->ws_session);
	}

	if (wstransport->transport.ref_cnt) {
		pj_atomic_destroy(wstransport->transport.ref_cnt);
	}

	if (wstransport->transport.lock) {
		pj_lock_destroy(wstransport->transport.lock);
	}

	if (wstransport->transport.endpt && wstransport->transport.pool) {
		pjsip_endpt_release_pool(wstransport->transport.endpt, wstransport->transport.pool);
	}

	if (wstransport->rdata.tp_info.pool) {
		pjsip_endpt_release_pool(wstransport->transport.endpt, wstransport->rdata.tp_info.pool);
	}
}

static int transport_shutdown(void *data)
{
	struct ws_transport *wstransport = data;

	if (!wstransport->transport.is_shutdown && !wstransport->transport.is_destroying) {
		pjsip_transport_shutdown(&wstransport->transport);
	}

	/* Note that the destructor calls PJSIP functions,
	 * therefore it must be called in a PJSIP thread.
	 */
	ao2_ref(wstransport, -1);

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
	struct ws_transport *newtransport = NULL;

	pjsip_endpoint *endpt = ast_sip_get_pjsip_endpoint();
	struct pjsip_tpmgr *tpmgr = pjsip_endpt_get_tpmgr(endpt);

	pj_pool_t *pool;
	pj_str_t buf;
	pj_status_t status;

	newtransport = ao2_t_alloc_options(sizeof(*newtransport), transport_dtor,
			AO2_ALLOC_OPT_LOCK_NOLOCK, "pjsip websocket transport");
	if (!newtransport) {
		ast_log(LOG_ERROR, "Failed to allocate WebSocket transport.\n");
		goto on_error;
	}

	newtransport->transport.endpt = endpt;

	if (!(pool = pjsip_endpt_create_pool(endpt, "ws", 512, 512))) {
		ast_log(LOG_ERROR, "Failed to allocate WebSocket endpoint pool.\n");
		goto on_error;
	}

	newtransport->transport.pool = pool;
	newtransport->ws_session = create_data->ws_session;

	/* Keep the session until transport dies */
	ast_websocket_ref(newtransport->ws_session);

	status = pj_atomic_create(pool, 0, &newtransport->transport.ref_cnt);
	if (status != PJ_SUCCESS) {
		goto on_error;
	}

	status = pj_lock_create_recursive_mutex(pool, pool->obj_name, &newtransport->transport.lock);
	if (status != PJ_SUCCESS) {
		goto on_error;
	}

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

	newtransport->transport.tpmgr = tpmgr;
	newtransport->transport.send_msg = &ws_send_msg;
	newtransport->transport.destroy = &ws_destroy;

	status = pjsip_transport_register(newtransport->transport.tpmgr,
			(pjsip_transport *)newtransport);
	if (status != PJ_SUCCESS) {
		goto on_error;
	}

	/* Add a reference for pjsip transport manager */
	ao2_ref(newtransport, +1);

	newtransport->rdata.tp_info.transport = &newtransport->transport;
	newtransport->rdata.tp_info.pool = pjsip_endpt_create_pool(endpt, "rtd%p",
		PJSIP_POOL_RDATA_LEN, PJSIP_POOL_RDATA_INC);
	if (!newtransport->rdata.tp_info.pool) {
		ast_log(LOG_ERROR, "Failed to allocate WebSocket rdata.\n");
		pjsip_transport_destroy((pjsip_transport *)newtransport);
		goto on_error;
	}

	create_data->transport = newtransport;
	return 0;

on_error:
	ao2_cleanup(newtransport);
	return -1;
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
	int pjsip_pkt_len;

	pj_gettimeofday(&rdata->pkt_info.timestamp);

	pjsip_pkt_len = PJSIP_MAX_PKT_LEN < read_data->payload_len ? PJSIP_MAX_PKT_LEN : read_data->payload_len;
	pj_memcpy(rdata->pkt_info.packet, read_data->payload, pjsip_pkt_len);
	rdata->pkt_info.len = pjsip_pkt_len;
	rdata->pkt_info.zero = 0;

	pj_sockaddr_parse(pj_AF_UNSPEC(), 0, pj_cstr(&buf, ast_sockaddr_stringify(ast_websocket_remote_address(session))), &rdata->pkt_info.src_addr);
	rdata->pkt_info.src_addr.addr.sa_family = pj_AF_INET();

	rdata->pkt_info.src_addr_len = sizeof(rdata->pkt_info.src_addr);

	pj_ansi_strcpy(rdata->pkt_info.src_name, ast_sockaddr_stringify_host(ast_websocket_remote_address(session)));
	rdata->pkt_info.src_port = ast_sockaddr_port(ast_websocket_remote_address(session));

	recvd = pjsip_tpmgr_receive_packet(rdata->tp_info.transport->tpmgr, rdata);

	pj_pool_reset(rdata->tp_info.pool);

	return (read_data->payload_len == recvd) ? 0 : -1;
}

static int get_write_timeout(void)
{
	int write_timeout = -1;
	struct ao2_container *transport_states;

	transport_states = ast_sip_get_transport_states();

	if (transport_states) {
		struct ao2_iterator it_transport_states = ao2_iterator_init(transport_states, 0);
		struct ast_sip_transport_state *transport_state;

		for (; (transport_state = ao2_iterator_next(&it_transport_states)); ao2_cleanup(transport_state)) {
			struct ast_sip_transport *transport;
			if (transport_state->type != AST_TRANSPORT_WS && transport_state->type != AST_TRANSPORT_WSS) {
				continue;
			}
			transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport", transport_state->id);
			ast_debug(5, "Found %s transport with write timeout: %d\n",
				transport->type == AST_TRANSPORT_WS ? "WS" : "WSS",
				transport->write_timeout);
			write_timeout = MAX(write_timeout, transport->write_timeout);
		}
		ao2_iterator_destroy(&it_transport_states);
		ao2_cleanup(transport_states);
	}

	if (write_timeout < 0) {
		write_timeout = AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT;
	}

	ast_debug(1, "Write timeout for WS/WSS transports: %d\n", write_timeout);
	return write_timeout;
}

static struct ast_taskprocessor *create_websocket_serializer(void)
{
	char tps_name[AST_TASKPROCESSOR_MAX_NAME + 1];

	/* Create name with seq number appended. */
	ast_taskprocessor_build_name(tps_name, sizeof(tps_name), "pjsip/websocket");

	return ast_sip_create_serializer(tps_name);
}

/*! \brief WebSocket connection handler. */
static void websocket_cb(struct ast_websocket *session, struct ast_variable *parameters, struct ast_variable *headers)
{
	struct ast_taskprocessor *serializer;
	struct transport_create_data create_data;
	struct ws_transport *transport;
	struct transport_read_data read_data;

	if (ast_websocket_set_nonblock(session)) {
		ast_websocket_unref(session);
		return;
	}

	if (ast_websocket_set_timeout(session, get_write_timeout())) {
		ast_websocket_unref(session);
		return;
	}

	serializer = create_websocket_serializer();
	if (!serializer) {
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

	if ((contact = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, NULL)) && !contact->star &&
		(PJSIP_URI_SCHEME_IS_SIP(contact->uri) || PJSIP_URI_SCHEME_IS_SIPS(contact->uri))) {
		pjsip_sip_uri *uri = pjsip_uri_get_uri(contact->uri);

		pj_cstr(&uri->host, rdata->pkt_info.src_name);
		uri->port = rdata->pkt_info.src_port;
		ast_debug(4, "Re-wrote Contact URI host/port to %.*s:%d\n",
			(int)pj_strlen(&uri->host), pj_strbuf(&uri->host), uri->port);
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
	.on_rx_response = websocket_on_rx_msg,
};

/*! \brief Function called when an INVITE goes out */
static void websocket_outgoing_invite_request(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	if (session->inv_session->state == PJSIP_INV_STATE_NULL) {
		pjsip_dlg_add_usage(session->inv_session->dlg, &websocket_module, NULL);
	}
}

/*! \brief Supplement for adding Websocket functionality to dialog */
static struct ast_sip_session_supplement websocket_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_FIRST + 1,
	.outgoing_request = websocket_outgoing_invite_request,
};

static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	pjsip_transport_register_type(PJSIP_TRANSPORT_RELIABLE, "WS", 5060, &transport_type_ws);
	pjsip_transport_register_type(PJSIP_TRANSPORT_RELIABLE | PJSIP_TRANSPORT_SECURE, "WSS", 5060, &transport_type_wss);

	if (ast_sip_register_service(&websocket_module) != PJ_SUCCESS) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sip_session_register_supplement(&websocket_supplement)) {
		ast_sip_unregister_service(&websocket_module);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_websocket_add_protocol("sip", websocket_cb)) {
		ast_sip_session_unregister_supplement(&websocket_supplement);
		ast_sip_unregister_service(&websocket_module);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_service(&websocket_module);
	ast_sip_session_unregister_supplement(&websocket_supplement);
	ast_websocket_remove_protocol("sip", websocket_cb);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP WebSocket Transport Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
);
