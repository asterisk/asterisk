/* $Id$ */
/* 
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */


/**
 * sipecho.c
 *
 * - Accepts incoming calls and echoes back SDP and any media.
 * - Specify URI in cmdline argument to make call
 * - Accepts registration too!
 */

/* Include all headers. */
#include <pjsip.h>
#include <pjmedia/sdp.h>
#include <pjsip_ua.h>
#include <pjlib-util.h>
#include <pjlib.h>

/* For logging purpose. */
#define THIS_FILE   "sipecho.c"

#include "util.h"


/* Settings */
#define AF		pj_AF_INET() /* Change to pj_AF_INET6() for IPv6.
				      * PJ_HAS_IPV6 must be enabled and
				      * your system must support IPv6.  */
#define SIP_PORT	5060	     /* Listening SIP port		*/
#define MAX_CALLS	8

typedef struct call_t
{
    pjsip_inv_session	*inv;
} call_t;

static struct app_t
{
    pj_caching_pool	 cp;
    pj_pool_t		*pool;

    pjsip_endpoint	*sip_endpt;
    //pjmedia_endpt	*med_endpt;

    call_t		 call[MAX_CALLS];

    pj_bool_t		 quit;
    pj_thread_t		*worker_thread;

    pj_bool_t		 enable_msg_logging;
} app;

/*
 * Prototypes:
 */

static void call_on_media_update(pjsip_inv_session *inv, pj_status_t status);
static void call_on_state_changed(pjsip_inv_session *inv, pjsip_event *e);
static void call_on_rx_offer(pjsip_inv_session *inv, const pjmedia_sdp_session *offer);
static void call_on_forked(pjsip_inv_session *inv, pjsip_event *e);
static pj_bool_t on_rx_request( pjsip_rx_data *rdata );


/* This is a PJSIP module to be registered by application to handle
 * incoming requests outside any dialogs/transactions. The main purpose
 * here is to handle incoming INVITE request message, where we will
 * create a dialog and INVITE session for it.
 */
static pjsip_module mod_sipecho =
{
    NULL, NULL,			    /* prev, next.		*/
    { "mod-sipecho", 11 },	    /* Name.			*/
    -1,				    /* Id			*/
    PJSIP_MOD_PRIORITY_APPLICATION, /* Priority			*/
    NULL,			    /* load()			*/
    NULL,			    /* start()			*/
    NULL,			    /* stop()			*/
    NULL,			    /* unload()			*/
    &on_rx_request,		    /* on_rx_request()		*/
    NULL,			    /* on_rx_response()		*/
    NULL,			    /* on_tx_request.		*/
    NULL,			    /* on_tx_response()		*/
    NULL,			    /* on_tsx_state()		*/
};

/* Notification on incoming messages */
static pj_bool_t logging_on_rx_msg(pjsip_rx_data *rdata)
{
    if (!app.enable_msg_logging)
	return PJ_FALSE;

    PJ_LOG(3,(THIS_FILE, "RX %d bytes %s from %s %s:%d:\n"
			 "%.*s\n"
			 "--end msg--",
			 rdata->msg_info.len,
			 pjsip_rx_data_get_info(rdata),
			 rdata->tp_info.transport->type_name,
			 rdata->pkt_info.src_name,
			 rdata->pkt_info.src_port,
			 (int)rdata->msg_info.len,
			 rdata->msg_info.msg_buf));
    return PJ_FALSE;
}

/* Notification on outgoing messages */
static pj_status_t logging_on_tx_msg(pjsip_tx_data *tdata)
{
    if (!app.enable_msg_logging)
	return PJ_SUCCESS;

    PJ_LOG(3,(THIS_FILE, "TX %d bytes %s to %s %s:%d:\n"
			 "%.*s\n"
			 "--end msg--",
			 (tdata->buf.cur - tdata->buf.start),
			 pjsip_tx_data_get_info(tdata),
			 tdata->tp_info.transport->type_name,
			 tdata->tp_info.dst_name,
			 tdata->tp_info.dst_port,
			 (int)(tdata->buf.cur - tdata->buf.start),
			 tdata->buf.start));
    return PJ_SUCCESS;
}

/* The module instance. */
static pjsip_module msg_logger =
{
    NULL, NULL,				/* prev, next.		*/
    { "mod-msg-log", 13 },		/* Name.		*/
    -1,					/* Id			*/
    PJSIP_MOD_PRIORITY_TRANSPORT_LAYER-1,/* Priority	        */
    NULL,				/* load()		*/
    NULL,				/* start()		*/
    NULL,				/* stop()		*/
    NULL,				/* unload()		*/
    &logging_on_rx_msg,			/* on_rx_request()	*/
    &logging_on_rx_msg,			/* on_rx_response()	*/
    &logging_on_tx_msg,			/* on_tx_request.	*/
    &logging_on_tx_msg,			/* on_tx_response()	*/
    NULL,				/* on_tsx_state()	*/

};

static int worker_proc(void *arg)
{
    PJ_UNUSED_ARG(arg);

    while (!app.quit) {
	pj_time_val interval = { 0, 20 };
	pjsip_endpt_handle_events(app.sip_endpt, &interval);
    }

    return 0;
}

static void hangup_all(void)
{
    unsigned i;
    for (i=0; i<MAX_CALLS; ++i) {
    	call_t *call = &app.call[i];

    	if (call->inv && call->inv->state <= PJSIP_INV_STATE_CONFIRMED) {
    	    pj_status_t status;
    	    pjsip_tx_data *tdata;

    	    status = pjsip_inv_end_session(call->inv, PJSIP_SC_BUSY_HERE, NULL, &tdata);
    	    if (status==PJ_SUCCESS && tdata)
    		pjsip_inv_send_msg(call->inv, tdata);
    	}
    }
}

static void destroy_stack(void)
{
    enum { WAIT_CLEAR = 5000, WAIT_INTERVAL = 500 };
    unsigned i;

    PJ_LOG(3,(THIS_FILE, "Shutting down.."));

    /* Wait until all clear */
    hangup_all();
    for (i=0; i<WAIT_CLEAR/WAIT_INTERVAL; ++i) {
	unsigned j;

	for (j=0; j<MAX_CALLS; ++j) {
	    call_t *call = &app.call[j];
	    if (call->inv && call->inv->state <= PJSIP_INV_STATE_CONFIRMED)
		break;
	}

	if (j==MAX_CALLS)
	    return;

	pj_thread_sleep(WAIT_INTERVAL);
    }

    app.quit = PJ_TRUE;
    if (app.worker_thread) {
	pj_thread_join(app.worker_thread);
	app.worker_thread = NULL;
    }

    //if (app.med_endpt)
	//pjmedia_endpt_destroy(app.med_endpt);

    if (app.sip_endpt)
	pjsip_endpt_destroy(app.sip_endpt);

    if (app.pool)
	pj_pool_release(app.pool);

    dump_pool_usage(THIS_FILE, &app.cp);
    pj_caching_pool_destroy(&app.cp);
}

#define CHECK_STATUS()	do { if (status != PJ_SUCCESS) return status; } while (0)

static pj_status_t init_stack()
{
    pj_sockaddr addr;
    pjsip_inv_callback inv_cb;
    pj_status_t status;

    pj_log_set_level(5);

    status = pj_init();
    CHECK_STATUS();

    pj_log_set_level(3);

    status = pjlib_util_init();
    CHECK_STATUS();

    pj_caching_pool_init(&app.cp, NULL, 0);
    app.pool = pj_pool_create( &app.cp.factory, "sipecho", 512, 512, 0);

    status = pjsip_endpt_create(&app.cp.factory, NULL, &app.sip_endpt);
    CHECK_STATUS();

    pj_log_set_level(4);
    pj_sockaddr_init(AF, &addr, NULL, (pj_uint16_t)SIP_PORT);
    if (AF == pj_AF_INET()) {
	status = pjsip_udp_transport_start( app.sip_endpt, &addr.ipv4, NULL,
					    1, NULL);
    } else if (AF == pj_AF_INET6()) {
	status = pjsip_udp_transport_start6(app.sip_endpt, &addr.ipv6, NULL,
					    1, NULL);
    } else {
	status = PJ_EAFNOTSUP;
    }

    pj_log_set_level(3);
    CHECK_STATUS();

    status = pjsip_tsx_layer_init_module(app.sip_endpt) ||
	     pjsip_ua_init_module( app.sip_endpt, NULL );
    CHECK_STATUS();

    pj_bzero(&inv_cb, sizeof(inv_cb));
    inv_cb.on_state_changed = &call_on_state_changed;
    inv_cb.on_new_session = &call_on_forked;
    inv_cb.on_media_update = &call_on_media_update;
    inv_cb.on_rx_offer = &call_on_rx_offer;

    status = pjsip_inv_usage_init(app.sip_endpt, &inv_cb) ||
	     pjsip_100rel_init_module(app.sip_endpt) ||
	     pjsip_endpt_register_module( app.sip_endpt, &mod_sipecho) ||
	     pjsip_endpt_register_module( app.sip_endpt, &msg_logger) ||
	     //pjmedia_endpt_create(&app.cp.factory,
		//		  pjsip_endpt_get_ioqueue(app.sip_endpt),
		//		  0, &app.med_endpt) ||
             pj_thread_create(app.pool, "sipecho", &worker_proc, NULL, 0, 0,
                              &app.worker_thread);
    CHECK_STATUS();

    return PJ_SUCCESS;
}

static void destroy_call(call_t *call)
{
    call->inv = NULL;
}

static pjmedia_sdp_attr * find_remove_sdp_attrs(unsigned *cnt,
                                                pjmedia_sdp_attr *attr[],
                                                unsigned cnt_attr_to_remove,
                                                const char* attr_to_remove[])
{
    pjmedia_sdp_attr *found_attr = NULL;
    int i;

    for (i=0; i<(int)*cnt; ++i) {
	unsigned j;
	for (j=0; j<cnt_attr_to_remove; ++j) {
	    if (pj_strcmp2(&attr[i]->name, attr_to_remove[j])==0) {
		if (!found_attr) found_attr = attr[i];
		pj_array_erase(attr, sizeof(attr[0]), *cnt, i);
		--(*cnt);
		--i;
		break;
	    }
	}
    }

    return found_attr;
}

static pjmedia_sdp_session *create_answer(int call_num, pj_pool_t *pool,
                                          const pjmedia_sdp_session *offer)
{
    const char* dir_attrs[] = { "sendrecv", "sendonly", "recvonly", "inactive" };
    const char *ice_attrs[] = {"ice-pwd", "ice-ufrag", "candidate"};
    pjmedia_sdp_session *answer = pjmedia_sdp_session_clone(pool, offer);
    pjmedia_sdp_attr *sess_dir_attr = NULL;
    unsigned mi;

    PJ_LOG(3,(THIS_FILE, "Call %d: creating answer:", call_num));

    answer->name = pj_str("sipecho");
    sess_dir_attr = find_remove_sdp_attrs(&answer->attr_count, answer->attr,
                                          PJ_ARRAY_SIZE(dir_attrs),
                                          dir_attrs);

    for (mi=0; mi<answer->media_count; ++mi) {
	pjmedia_sdp_media *m = answer->media[mi];
	pjmedia_sdp_attr *m_dir_attr;
	pjmedia_sdp_attr *dir_attr;
	const char *our_dir = NULL;
	pjmedia_sdp_conn *c;

	/* Match direction */
	m_dir_attr = find_remove_sdp_attrs(&m->attr_count, m->attr,
	                                   PJ_ARRAY_SIZE(dir_attrs),
	                                   dir_attrs);
	dir_attr = m_dir_attr ? m_dir_attr : sess_dir_attr;

	if (dir_attr) {
	    if (pj_strcmp2(&dir_attr->name, "sendonly")==0)
		our_dir = "recvonly";
	    else if (pj_strcmp2(&dir_attr->name, "inactive")==0)
		our_dir = "inactive";
	    else if (pj_strcmp2(&dir_attr->name, "recvonly")==0)
		our_dir = "inactive";

	    if (our_dir) {
		dir_attr = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_attr);
		dir_attr->name = pj_str((char*)our_dir);
		m->attr[m->attr_count++] = dir_attr;
	    }
	}

	/* Remove ICE attributes */
	find_remove_sdp_attrs(&m->attr_count, m->attr, PJ_ARRAY_SIZE(ice_attrs), ice_attrs);

	/* Done */
	c = m->conn ? m->conn : answer->conn;
	PJ_LOG(3,(THIS_FILE, "  Media %d, %.*s: %s <--> %.*s:%d",
		  mi, (int)m->desc.media.slen, m->desc.media.ptr,
		  (our_dir ? our_dir : "sendrecv"),
		  (int)c->addr.slen, c->addr.ptr, m->desc.port));
    }

    return answer;
}

static void call_on_state_changed( pjsip_inv_session *inv, 
				   pjsip_event *e)
{
    call_t *call = (call_t*)inv->mod_data[mod_sipecho.id];
    if (!call)
	return;

    PJ_UNUSED_ARG(e);
    if (inv->state == PJSIP_INV_STATE_DISCONNECTED) {
	PJ_LOG(3,(THIS_FILE, "Call %d: DISCONNECTED [reason=%d (%s)]",
		  call - app.call, inv->cause,
		  pjsip_get_status_text(inv->cause)->ptr));
	destroy_call(call);
    } else {
	PJ_LOG(3,(THIS_FILE, "Call %d: state changed to %s",
		  call - app.call, pjsip_inv_state_name(inv->state)));
    }
}

static void call_on_rx_offer(pjsip_inv_session *inv, const pjmedia_sdp_session *offer)
{
    call_t *call = (call_t*) inv->mod_data[mod_sipecho.id];
    pjsip_inv_set_sdp_answer(inv, create_answer(call - app.call, inv->pool_prov, offer));
}

static void call_on_forked(pjsip_inv_session *inv, pjsip_event *e)
{
    PJ_UNUSED_ARG(inv);
    PJ_UNUSED_ARG(e);
}

static pj_bool_t on_rx_request( pjsip_rx_data *rdata )
{
    pj_sockaddr hostaddr;
    char temp[80], hostip[PJ_INET6_ADDRSTRLEN];
    pj_str_t local_uri;
    pjsip_dialog *dlg;
    pjsip_rdata_sdp_info *sdp_info;
    pjmedia_sdp_session *answer = NULL;
    pjsip_tx_data *tdata = NULL;
    call_t *call = NULL;
    unsigned i;
    pj_status_t status;

    PJ_LOG(3,(THIS_FILE, "RX %.*s from %s",
	      (int)rdata->msg_info.msg->line.req.method.name.slen,
	      rdata->msg_info.msg->line.req.method.name.ptr,
	      rdata->pkt_info.src_name));

    if (rdata->msg_info.msg->line.req.method.id == PJSIP_REGISTER_METHOD) {
	/* Let me be a registrar! */
	pjsip_hdr hdr_list, *h;
	pjsip_msg *msg;
	int expires = -1;

	pj_list_init(&hdr_list);
	msg = rdata->msg_info.msg;
	h = (pjsip_hdr*)pjsip_msg_find_hdr(msg, PJSIP_H_EXPIRES, NULL);
	if (h) {
	    expires = ((pjsip_expires_hdr*)h)->ivalue;
	    pj_list_push_back(&hdr_list, pjsip_hdr_clone(rdata->tp_info.pool, h));
	    PJ_LOG(3,(THIS_FILE, " Expires=%d", expires));
	}
	if (expires != 0) {
	    h = (pjsip_hdr*)pjsip_msg_find_hdr(msg, PJSIP_H_CONTACT, NULL);
	    if (h)
		pj_list_push_back(&hdr_list, pjsip_hdr_clone(rdata->tp_info.pool, h));
	}

	pjsip_endpt_respond(app.sip_endpt, &mod_sipecho, rdata, 200, NULL,
	                    &hdr_list, NULL, NULL);
	return PJ_TRUE;
    }

    if (rdata->msg_info.msg->line.req.method.id != PJSIP_INVITE_METHOD) {
	if (rdata->msg_info.msg->line.req.method.id != PJSIP_ACK_METHOD) {
	    pj_str_t reason = pj_str("Go away");
	    pjsip_endpt_respond_stateless( app.sip_endpt, rdata,
					   400, &reason,
					   NULL, NULL);
	}
	return PJ_TRUE;
    }

    sdp_info = pjsip_rdata_get_sdp_info(rdata);
    if (!sdp_info || !sdp_info->sdp) {
	pj_str_t reason = pj_str("Require valid offer");
	pjsip_endpt_respond_stateless( app.sip_endpt, rdata,
				       400, &reason,
				       NULL, NULL);
    }

    for (i=0; i<MAX_CALLS; ++i) {
	if (app.call[i].inv == NULL) {
	    call = &app.call[i];
	    break;
	}
    }

    if (i==MAX_CALLS) {
	pj_str_t reason = pj_str("We're full");
	pjsip_endpt_respond_stateless( app.sip_endpt, rdata,
				       PJSIP_SC_BUSY_HERE, &reason,
				       NULL, NULL);
	return PJ_TRUE;
    }

    /* Generate Contact URI */
    status = pj_gethostip(AF, &hostaddr);
    if (status != PJ_SUCCESS) {
	app_perror(THIS_FILE, "Unable to retrieve local host IP", status);
	return PJ_TRUE;
    }
    pj_sockaddr_print(&hostaddr, hostip, sizeof(hostip), 2);
    pj_ansi_sprintf(temp, "<sip:sipecho@%s:%d>", hostip, SIP_PORT);
    local_uri = pj_str(temp);

    status = pjsip_dlg_create_uas( pjsip_ua_instance(), rdata,
				   &local_uri, &dlg);

    if (status == PJ_SUCCESS)
	answer = create_answer(call-app.call, dlg->pool, sdp_info->sdp);
    if (status == PJ_SUCCESS)
    	status = pjsip_inv_create_uas( dlg, rdata, answer, 0, &call->inv);
    if (status == PJ_SUCCESS)
    	status = pjsip_inv_initial_answer(call->inv, rdata, 100,
				          NULL, NULL, &tdata);
    if (status == PJ_SUCCESS)
    	status = pjsip_inv_send_msg(call->inv, tdata);

    if (status == PJ_SUCCESS)
    	status = pjsip_inv_answer(call->inv, 180, NULL,
    	                          NULL, &tdata);
    if (status == PJ_SUCCESS)
    	status = pjsip_inv_send_msg(call->inv, tdata);

    if (status == PJ_SUCCESS)
    	status = pjsip_inv_answer(call->inv, 200, NULL,
    	                          NULL, &tdata);
    if (status == PJ_SUCCESS)
    	status = pjsip_inv_send_msg(call->inv, tdata);

    if (status != PJ_SUCCESS) {
	pjsip_endpt_respond_stateless( app.sip_endpt, rdata,
				       500, NULL, NULL, NULL);
	destroy_call(call);
    } else {
	call->inv->mod_data[mod_sipecho.id] = call;
    }

    return PJ_TRUE;
}

static void call_on_media_update( pjsip_inv_session *inv,
				  pj_status_t status)
{
    PJ_UNUSED_ARG(inv);
    PJ_UNUSED_ARG(status);
}


/* main()
 *
 * If called with argument, treat argument as SIP URL to be called.
 * Otherwise wait for incoming calls.
 */
int main(int argc, char *argv[])
{
    if (init_stack())
	goto on_error;

    /* If URL is specified, then make call immediately. */
    if (argc > 1) {
	pj_sockaddr hostaddr;
	char hostip[PJ_INET6_ADDRSTRLEN+2];
	char temp[80];
	call_t *call;
	pj_str_t dst_uri = pj_str(argv[1]);
	pj_str_t local_uri;
	pjsip_dialog *dlg;
	pj_status_t status;
	pjsip_tx_data *tdata;

	if (pj_gethostip(AF, &hostaddr) != PJ_SUCCESS) {
	    PJ_LOG(1,(THIS_FILE, "Unable to retrieve local host IP"));
	    goto on_error;
	}
	pj_sockaddr_print(&hostaddr, hostip, sizeof(hostip), 2);

	pj_ansi_sprintf(temp, "<sip:sipecho@%s:%d>",
			hostip, SIP_PORT);
	local_uri = pj_str(temp);

	call = &app.call[0];

	status = pjsip_dlg_create_uac( pjsip_ua_instance(),
				       &local_uri,  /* local URI */
				       &local_uri,  /* local Contact */
				       &dst_uri,    /* remote URI */
				       &dst_uri,    /* remote target */
				       &dlg);	    /* dialog */
	if (status != PJ_SUCCESS) {
	    app_perror(THIS_FILE, "Unable to create UAC dialog", status);
	    return 1;
	}

	status = pjsip_inv_create_uac( dlg, NULL, 0, &call->inv);
	if (status != PJ_SUCCESS) goto on_error;

	call->inv->mod_data[mod_sipecho.id] = call;

	status = pjsip_inv_invite(call->inv, &tdata);
	if (status != PJ_SUCCESS) goto on_error;

	status = pjsip_inv_send_msg(call->inv, tdata);
	if (status != PJ_SUCCESS) goto on_error;

	puts("Press ENTER to quit...");
    } else {
	puts("Ready for incoming calls. Press ENTER to quit...");
    }

    for (;;) {
	char s[10];

	printf("\nMenu:\n"
	       "  h    Hangup all calls\n"
	       "  l    %s message logging\n"
	       "  q    Quit\n",
	       (app.enable_msg_logging? "Disable" : "Enable"));

	if (fgets(s, sizeof(s), stdin) == NULL)
	    continue;

	if (s[0]=='q')
	    break;
	switch (s[0]) {
	case 'l':
	    app.enable_msg_logging = !app.enable_msg_logging;
	    break;
	case 'h':
	    hangup_all();
	    break;
	}
    }

    destroy_stack();

    puts("Bye bye..");
    return 0;

on_error:
    puts("An error has occurred. run a debugger..");
    return 1;
}

