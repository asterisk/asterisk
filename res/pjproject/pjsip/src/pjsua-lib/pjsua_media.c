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
#include <pjsua-lib/pjsua.h>
#include <pjsua-lib/pjsua_internal.h>


#define THIS_FILE		"pjsua_media.c"

#define DEFAULT_RTP_PORT	4000

#ifndef PJSUA_REQUIRE_CONSECUTIVE_RTCP_PORT
#   define PJSUA_REQUIRE_CONSECUTIVE_RTCP_PORT	0
#endif

/* Next RTP port to be used */
static pj_uint16_t next_rtp_port;

static void pjsua_media_config_dup(pj_pool_t *pool,
				   pjsua_media_config *dst,
				   const pjsua_media_config *src)
{
    pj_memcpy(dst, src, sizeof(*src));
    pj_strdup(pool, &dst->turn_server, &src->turn_server);
    pj_stun_auth_cred_dup(pool, &dst->turn_auth_cred, &src->turn_auth_cred);
}


/**
 * Init media subsystems.
 */
pj_status_t pjsua_media_subsys_init(const pjsua_media_config *cfg)
{
    pj_status_t status;

    pj_log_push_indent();

    /* Specify which audio device settings are save-able */
    pjsua_var.aud_svmask = 0xFFFFFFFF;
    /* These are not-settable */
    pjsua_var.aud_svmask &= ~(PJMEDIA_AUD_DEV_CAP_EXT_FORMAT |
			      PJMEDIA_AUD_DEV_CAP_INPUT_SIGNAL_METER |
			      PJMEDIA_AUD_DEV_CAP_OUTPUT_SIGNAL_METER);
    /* EC settings use different API */
    pjsua_var.aud_svmask &= ~(PJMEDIA_AUD_DEV_CAP_EC |
			      PJMEDIA_AUD_DEV_CAP_EC_TAIL);

    /* Copy configuration */
    pjsua_media_config_dup(pjsua_var.pool, &pjsua_var.media_cfg, cfg);

    /* Normalize configuration */
    if (pjsua_var.media_cfg.snd_clock_rate == 0) {
	pjsua_var.media_cfg.snd_clock_rate = pjsua_var.media_cfg.clock_rate;
    }

    if (pjsua_var.media_cfg.has_ioqueue &&
	pjsua_var.media_cfg.thread_cnt == 0)
    {
	pjsua_var.media_cfg.thread_cnt = 1;
    }

    if (pjsua_var.media_cfg.max_media_ports < pjsua_var.ua_cfg.max_calls) {
	pjsua_var.media_cfg.max_media_ports = pjsua_var.ua_cfg.max_calls + 2;
    }

    /* Create media endpoint. */
    status = pjmedia_endpt_create(&pjsua_var.cp.factory, 
				  pjsua_var.media_cfg.has_ioqueue? NULL :
				     pjsip_endpt_get_ioqueue(pjsua_var.endpt),
				  pjsua_var.media_cfg.thread_cnt,
				  &pjsua_var.med_endpt);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, 
		     "Media stack initialization has returned error", 
		     status);
	goto on_error;
    }

    status = pjsua_aud_subsys_init();
    if (status != PJ_SUCCESS)
	goto on_error;

#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
    /* Initialize SRTP library (ticket #788). */
    status = pjmedia_srtp_init_lib(pjsua_var.med_endpt);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error initializing SRTP library", 
		     status);
	goto on_error;
    }
#endif

    /* Video */
#if PJMEDIA_HAS_VIDEO
    status = pjsua_vid_subsys_init();
    if (status != PJ_SUCCESS)
	goto on_error;
#endif

    pj_log_pop_indent();
    return PJ_SUCCESS;

on_error:
    pj_log_pop_indent();
    return status;
}

/*
 * Start pjsua media subsystem.
 */
pj_status_t pjsua_media_subsys_start(void)
{
    pj_status_t status;

    pj_log_push_indent();

#if DISABLED_FOR_TICKET_1185
    /* Create media for calls, if none is specified */
    if (pjsua_var.calls[0].media[0].tp == NULL) {
	pjsua_transport_config transport_cfg;

	/* Create default transport config */
	pjsua_transport_config_default(&transport_cfg);
	transport_cfg.port = DEFAULT_RTP_PORT;

	status = pjsua_media_transports_create(&transport_cfg);
	if (status != PJ_SUCCESS) {
	    pj_log_pop_indent();
	    return status;
	}
    }
#endif

    /* Audio */
    status = pjsua_aud_subsys_start();
    if (status != PJ_SUCCESS) {
	pj_log_pop_indent();
	return status;
    }

    /* Video */
#if PJMEDIA_HAS_VIDEO
    status = pjsua_vid_subsys_start();
    if (status != PJ_SUCCESS) {
	pjsua_aud_subsys_destroy();
	pj_log_pop_indent();
	return status;
    }
#endif

    /* Perform NAT detection */
    status = pjsua_detect_nat_type();
    if (status != PJ_SUCCESS) {
	PJ_PERROR(1,(THIS_FILE, status, "NAT type detection failed"));
    }

    pj_log_pop_indent();
    return PJ_SUCCESS;
}


/*
 * Destroy pjsua media subsystem.
 */
pj_status_t pjsua_media_subsys_destroy(unsigned flags)
{
    unsigned i;

    PJ_LOG(4,(THIS_FILE, "Shutting down media.."));
    pj_log_push_indent();

    if (pjsua_var.med_endpt) {
	pjsua_aud_subsys_destroy();
    }

    /* Close media transports */
    for (i=0; i<pjsua_var.ua_cfg.max_calls; ++i) {
        /* TODO: check if we're not allowed to send to network in the
         *       "flags", and if so do not do TURN allocation...
         */
	PJ_UNUSED_ARG(flags);
	pjsua_media_channel_deinit(i);
    }

    /* Destroy media endpoint. */
    if (pjsua_var.med_endpt) {

#	if PJMEDIA_HAS_VIDEO
	    pjsua_vid_subsys_destroy();
#	endif

	pjmedia_endpt_destroy(pjsua_var.med_endpt);
	pjsua_var.med_endpt = NULL;

	/* Deinitialize sound subsystem */
	// Not necessary, as pjmedia_snd_deinit() should have been called
	// in pjmedia_endpt_destroy().
	//pjmedia_snd_deinit();
    }

    /* Reset RTP port */
    next_rtp_port = 0;

    pj_log_pop_indent();

    return PJ_SUCCESS;
}

/*
 * Create RTP and RTCP socket pair, and possibly resolve their public
 * address via STUN.
 */
static pj_status_t create_rtp_rtcp_sock(const pjsua_transport_config *cfg,
					pjmedia_sock_info *skinfo)
{
    enum {
	RTP_RETRY = 100
    };
    int i;
    pj_sockaddr_in bound_addr;
    pj_sockaddr_in mapped_addr[2];
    pj_status_t status = PJ_SUCCESS;
    char addr_buf[PJ_INET6_ADDRSTRLEN+2];
    pj_sock_t sock[2];

    /* Make sure STUN server resolution has completed */
    status = resolve_stun_server(PJ_TRUE);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error resolving STUN server", status);
	return status;
    }

    if (next_rtp_port == 0)
	next_rtp_port = (pj_uint16_t)cfg->port;

    if (next_rtp_port == 0)
	next_rtp_port = (pj_uint16_t)40000;

    for (i=0; i<2; ++i)
	sock[i] = PJ_INVALID_SOCKET;

    bound_addr.sin_addr.s_addr = PJ_INADDR_ANY;
    if (cfg->bound_addr.slen) {
	status = pj_sockaddr_in_set_str_addr(&bound_addr, &cfg->bound_addr);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to resolve transport bind address",
			 status);
	    return status;
	}
    }

    /* Loop retry to bind RTP and RTCP sockets. */
    for (i=0; i<RTP_RETRY; ++i, next_rtp_port += 2) {

	/* Create RTP socket. */
	status = pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(), 0, &sock[0]);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "socket() error", status);
	    return status;
	}

	/* Apply QoS to RTP socket, if specified */
	status = pj_sock_apply_qos2(sock[0], cfg->qos_type,
				    &cfg->qos_params,
				    2, THIS_FILE, "RTP socket");

	/* Bind RTP socket */
	status=pj_sock_bind_in(sock[0], pj_ntohl(bound_addr.sin_addr.s_addr),
			       next_rtp_port);
	if (status != PJ_SUCCESS) {
	    pj_sock_close(sock[0]);
	    sock[0] = PJ_INVALID_SOCKET;
	    continue;
	}

	/* Create RTCP socket. */
	status = pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(), 0, &sock[1]);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "socket() error", status);
	    pj_sock_close(sock[0]);
	    return status;
	}

	/* Apply QoS to RTCP socket, if specified */
	status = pj_sock_apply_qos2(sock[1], cfg->qos_type,
				    &cfg->qos_params,
				    2, THIS_FILE, "RTCP socket");

	/* Bind RTCP socket */
	status=pj_sock_bind_in(sock[1], pj_ntohl(bound_addr.sin_addr.s_addr),
			       (pj_uint16_t)(next_rtp_port+1));
	if (status != PJ_SUCCESS) {
	    pj_sock_close(sock[0]);
	    sock[0] = PJ_INVALID_SOCKET;

	    pj_sock_close(sock[1]);
	    sock[1] = PJ_INVALID_SOCKET;
	    continue;
	}

	/*
	 * If we're configured to use STUN, then find out the mapped address,
	 * and make sure that the mapped RTCP port is adjacent with the RTP.
	 */
	if (pjsua_var.stun_srv.addr.sa_family != 0) {
	    char ip_addr[32];
	    pj_str_t stun_srv;

	    pj_ansi_strcpy(ip_addr,
			   pj_inet_ntoa(pjsua_var.stun_srv.ipv4.sin_addr));
	    stun_srv = pj_str(ip_addr);

	    status=pjstun_get_mapped_addr(&pjsua_var.cp.factory, 2, sock,
					   &stun_srv, pj_ntohs(pjsua_var.stun_srv.ipv4.sin_port),
					   &stun_srv, pj_ntohs(pjsua_var.stun_srv.ipv4.sin_port),
					   mapped_addr);
	    if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "STUN resolve error", status);
		goto on_error;
	    }

#if PJSUA_REQUIRE_CONSECUTIVE_RTCP_PORT
	    if (pj_ntohs(mapped_addr[1].sin_port) ==
		pj_ntohs(mapped_addr[0].sin_port)+1)
	    {
		/* Success! */
		break;
	    }

	    pj_sock_close(sock[0]);
	    sock[0] = PJ_INVALID_SOCKET;

	    pj_sock_close(sock[1]);
	    sock[1] = PJ_INVALID_SOCKET;
#else
	    if (pj_ntohs(mapped_addr[1].sin_port) !=
		pj_ntohs(mapped_addr[0].sin_port)+1)
	    {
		PJ_LOG(4,(THIS_FILE,
			  "Note: STUN mapped RTCP port %d is not adjacent"
			  " to RTP port %d",
			  pj_ntohs(mapped_addr[1].sin_port),
			  pj_ntohs(mapped_addr[0].sin_port)));
	    }
	    /* Success! */
	    break;
#endif

	} else if (cfg->public_addr.slen) {

	    status = pj_sockaddr_in_init(&mapped_addr[0], &cfg->public_addr,
					 (pj_uint16_t)next_rtp_port);
	    if (status != PJ_SUCCESS)
		goto on_error;

	    status = pj_sockaddr_in_init(&mapped_addr[1], &cfg->public_addr,
					 (pj_uint16_t)(next_rtp_port+1));
	    if (status != PJ_SUCCESS)
		goto on_error;

	    break;

	} else {

	    if (bound_addr.sin_addr.s_addr == 0) {
		pj_sockaddr addr;

		/* Get local IP address. */
		status = pj_gethostip(pj_AF_INET(), &addr);
		if (status != PJ_SUCCESS)
		    goto on_error;

		bound_addr.sin_addr.s_addr = addr.ipv4.sin_addr.s_addr;
	    }

	    for (i=0; i<2; ++i) {
		pj_sockaddr_in_init(&mapped_addr[i], NULL, 0);
		mapped_addr[i].sin_addr.s_addr = bound_addr.sin_addr.s_addr;
	    }

	    mapped_addr[0].sin_port=pj_htons((pj_uint16_t)next_rtp_port);
	    mapped_addr[1].sin_port=pj_htons((pj_uint16_t)(next_rtp_port+1));
	    break;
	}
    }

    if (sock[0] == PJ_INVALID_SOCKET) {
	PJ_LOG(1,(THIS_FILE,
		  "Unable to find appropriate RTP/RTCP ports combination"));
	goto on_error;
    }


    skinfo->rtp_sock = sock[0];
    pj_memcpy(&skinfo->rtp_addr_name,
	      &mapped_addr[0], sizeof(pj_sockaddr_in));

    skinfo->rtcp_sock = sock[1];
    pj_memcpy(&skinfo->rtcp_addr_name,
	      &mapped_addr[1], sizeof(pj_sockaddr_in));

    PJ_LOG(4,(THIS_FILE, "RTP socket reachable at %s",
	      pj_sockaddr_print(&skinfo->rtp_addr_name, addr_buf,
				sizeof(addr_buf), 3)));
    PJ_LOG(4,(THIS_FILE, "RTCP socket reachable at %s",
	      pj_sockaddr_print(&skinfo->rtcp_addr_name, addr_buf,
				sizeof(addr_buf), 3)));

    next_rtp_port += 2;
    return PJ_SUCCESS;

on_error:
    for (i=0; i<2; ++i) {
	if (sock[i] != PJ_INVALID_SOCKET)
	    pj_sock_close(sock[i]);
    }
    return status;
}

/* Create normal UDP media transports */
static pj_status_t create_udp_media_transport(const pjsua_transport_config *cfg,
					      pjsua_call_media *call_med)
{
    pjmedia_sock_info skinfo;
    pj_status_t status;

    status = create_rtp_rtcp_sock(cfg, &skinfo);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create RTP/RTCP socket",
		     status);
	goto on_error;
    }

    status = pjmedia_transport_udp_attach(pjsua_var.med_endpt, NULL,
					  &skinfo, 0, &call_med->tp);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create media transport",
		     status);
	goto on_error;
    }

    pjmedia_transport_simulate_lost(call_med->tp, PJMEDIA_DIR_ENCODING,
				    pjsua_var.media_cfg.tx_drop_pct);

    pjmedia_transport_simulate_lost(call_med->tp, PJMEDIA_DIR_DECODING,
				    pjsua_var.media_cfg.rx_drop_pct);

    call_med->tp_ready = PJ_SUCCESS;

    return PJ_SUCCESS;

on_error:
    if (call_med->tp)
	pjmedia_transport_close(call_med->tp);

    return status;
}

#if DISABLED_FOR_TICKET_1185
/* Create normal UDP media transports */
static pj_status_t create_udp_media_transports(pjsua_transport_config *cfg)
{
    unsigned i;
    pj_status_t status;

    for (i=0; i < pjsua_var.ua_cfg.max_calls; ++i) {
	pjsua_call *call = &pjsua_var.calls[i];
	unsigned strm_idx;

	for (strm_idx=0; strm_idx < call->med_cnt; ++strm_idx) {
	    pjsua_call_media *call_med = &call->media[strm_idx];

	    status = create_udp_media_transport(cfg, &call_med->tp);
	    if (status != PJ_SUCCESS)
		goto on_error;
	}
    }

    return PJ_SUCCESS;

on_error:
    for (i=0; i < pjsua_var.ua_cfg.max_calls; ++i) {
	pjsua_call *call = &pjsua_var.calls[i];
	unsigned strm_idx;

	for (strm_idx=0; strm_idx < call->med_cnt; ++strm_idx) {
	    pjsua_call_media *call_med = &call->media[strm_idx];

	    if (call_med->tp) {
		pjmedia_transport_close(call_med->tp);
		call_med->tp = NULL;
	    }
	}
    }
    return status;
}
#endif

static void med_tp_timer_cb(void *user_data)
{
    pjsua_call_media *call_med = (pjsua_call_media*)user_data;

    PJSUA_LOCK();

    call_med->tp_ready = call_med->tp_result;
    if (call_med->med_create_cb)
        (*call_med->med_create_cb)(call_med, call_med->tp_ready,
                                   call_med->call->secure_level, NULL);

    PJSUA_UNLOCK();
}

/* This callback is called when ICE negotiation completes */
static void on_ice_complete(pjmedia_transport *tp, 
			    pj_ice_strans_op op,
			    pj_status_t result)
{
    pjsua_call_media *call_med = (pjsua_call_media*)tp->user_data;

    if (!call_med)
	return;

    switch (op) {
    case PJ_ICE_STRANS_OP_INIT:
        call_med->tp_result = result;
        pjsua_schedule_timer2(&med_tp_timer_cb, call_med, 1);
	break;
    case PJ_ICE_STRANS_OP_NEGOTIATION:
	if (result != PJ_SUCCESS) {
	    call_med->state = PJSUA_CALL_MEDIA_ERROR;
	    call_med->dir = PJMEDIA_DIR_NONE;

	    if (call_med->call && pjsua_var.ua_cfg.cb.on_call_media_state) {
		pjsua_var.ua_cfg.cb.on_call_media_state(call_med->call->index);
	    }
	} else if (call_med->call) {
	    /* Send UPDATE if default transport address is different than
	     * what was advertised (ticket #881)
	     */
	    pjmedia_transport_info tpinfo;
	    pjmedia_ice_transport_info *ii = NULL;
	    unsigned i;

	    pjmedia_transport_info_init(&tpinfo);
	    pjmedia_transport_get_info(tp, &tpinfo);
	    for (i=0; i<tpinfo.specific_info_cnt; ++i) {
		if (tpinfo.spc_info[i].type==PJMEDIA_TRANSPORT_TYPE_ICE) {
		    ii = (pjmedia_ice_transport_info*)
			 tpinfo.spc_info[i].buffer;
		    break;
		}
	    }

	    if (ii && ii->role==PJ_ICE_SESS_ROLE_CONTROLLING &&
		pj_sockaddr_cmp(&tpinfo.sock_info.rtp_addr_name,
				&call_med->rtp_addr))
	    {
		pj_bool_t use_update;
		const pj_str_t STR_UPDATE = { "UPDATE", 6 };
		pjsip_dialog_cap_status support_update;
		pjsip_dialog *dlg;

		dlg = call_med->call->inv->dlg;
		support_update = pjsip_dlg_remote_has_cap(dlg, PJSIP_H_ALLOW,
							  NULL, &STR_UPDATE);
		use_update = (support_update == PJSIP_DIALOG_CAP_SUPPORTED);

		PJ_LOG(4,(THIS_FILE, 
		          "ICE default transport address has changed for "
			  "call %d, sending %s",
			  call_med->call->index,
			  (use_update ? "UPDATE" : "re-INVITE")));

		if (use_update)
		    pjsua_call_update(call_med->call->index, 0, NULL);
		else
		    pjsua_call_reinvite(call_med->call->index, 0, NULL);
	    }
	}
	break;
    case PJ_ICE_STRANS_OP_KEEP_ALIVE:
	if (result != PJ_SUCCESS) {
	    PJ_PERROR(4,(THIS_FILE, result,
		         "ICE keep alive failure for transport %d:%d",
		         call_med->call->index, call_med->idx));
	}
        if (pjsua_var.ua_cfg.cb.on_call_media_transport_state) {
            pjsua_med_tp_state_info info;

            pj_bzero(&info, sizeof(info));
            info.med_idx = call_med->idx;
            info.state = call_med->tp_st;
            info.status = result;
            info.ext_info = &op;
	    (*pjsua_var.ua_cfg.cb.on_call_media_transport_state)(
                call_med->call->index, &info);
        }
	if (pjsua_var.ua_cfg.cb.on_ice_transport_error) {
	    pjsua_call_id id = call_med->call->index;
	    (*pjsua_var.ua_cfg.cb.on_ice_transport_error)(id, op, result,
							  NULL);
	}
	break;
    }
}


/* Parse "HOST:PORT" format */
static pj_status_t parse_host_port(const pj_str_t *host_port,
				   pj_str_t *host, pj_uint16_t *port)
{
    pj_str_t str_port;

    str_port.ptr = pj_strchr(host_port, ':');
    if (str_port.ptr != NULL) {
	int iport;

	host->ptr = host_port->ptr;
	host->slen = (str_port.ptr - host->ptr);
	str_port.ptr++;
	str_port.slen = host_port->slen - host->slen - 1;
	iport = (int)pj_strtoul(&str_port);
	if (iport < 1 || iport > 65535)
	    return PJ_EINVAL;
	*port = (pj_uint16_t)iport;
    } else {
	*host = *host_port;
	*port = 0;
    }

    return PJ_SUCCESS;
}

/* Create ICE media transports (when ice is enabled) */
static pj_status_t create_ice_media_transport(
				const pjsua_transport_config *cfg,
				pjsua_call_media *call_med,
                                pj_bool_t async)
{
    char stunip[PJ_INET6_ADDRSTRLEN];
    pj_ice_strans_cfg ice_cfg;
    pjmedia_ice_cb ice_cb;
    char name[32];
    unsigned comp_cnt;
    pj_status_t status;

    /* Make sure STUN server resolution has completed */
    status = resolve_stun_server(PJ_TRUE);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error resolving STUN server", status);
	return status;
    }

    /* Create ICE stream transport configuration */
    pj_ice_strans_cfg_default(&ice_cfg);
    pj_stun_config_init(&ice_cfg.stun_cfg, &pjsua_var.cp.factory, 0,
		        pjsip_endpt_get_ioqueue(pjsua_var.endpt),
			pjsip_endpt_get_timer_heap(pjsua_var.endpt));
    
    ice_cfg.af = pj_AF_INET();
    ice_cfg.resolver = pjsua_var.resolver;
    
    ice_cfg.opt = pjsua_var.media_cfg.ice_opt;

    /* Configure STUN settings */
    if (pj_sockaddr_has_addr(&pjsua_var.stun_srv)) {
	pj_sockaddr_print(&pjsua_var.stun_srv, stunip, sizeof(stunip), 0);
	ice_cfg.stun.server = pj_str(stunip);
	ice_cfg.stun.port = pj_sockaddr_get_port(&pjsua_var.stun_srv);
    }
    if (pjsua_var.media_cfg.ice_max_host_cands >= 0)
	ice_cfg.stun.max_host_cands = pjsua_var.media_cfg.ice_max_host_cands;

    /* Copy QoS setting to STUN setting */
    ice_cfg.stun.cfg.qos_type = cfg->qos_type;
    pj_memcpy(&ice_cfg.stun.cfg.qos_params, &cfg->qos_params,
	      sizeof(cfg->qos_params));

    /* Configure TURN settings */
    if (pjsua_var.media_cfg.enable_turn) {
	status = parse_host_port(&pjsua_var.media_cfg.turn_server,
				 &ice_cfg.turn.server,
				 &ice_cfg.turn.port);
	if (status != PJ_SUCCESS || ice_cfg.turn.server.slen == 0) {
	    PJ_LOG(1,(THIS_FILE, "Invalid TURN server setting"));
	    return PJ_EINVAL;
	}
	if (ice_cfg.turn.port == 0)
	    ice_cfg.turn.port = 3479;
	ice_cfg.turn.conn_type = pjsua_var.media_cfg.turn_conn_type;
	pj_memcpy(&ice_cfg.turn.auth_cred, 
		  &pjsua_var.media_cfg.turn_auth_cred,
		  sizeof(ice_cfg.turn.auth_cred));

	/* Copy QoS setting to TURN setting */
	ice_cfg.turn.cfg.qos_type = cfg->qos_type;
	pj_memcpy(&ice_cfg.turn.cfg.qos_params, &cfg->qos_params,
		  sizeof(cfg->qos_params));
    }

    pj_bzero(&ice_cb, sizeof(pjmedia_ice_cb));
    ice_cb.on_ice_complete = &on_ice_complete;
    pj_ansi_snprintf(name, sizeof(name), "icetp%02d", call_med->idx);
    call_med->tp_ready = PJ_EPENDING;

    comp_cnt = 1;
    if (PJMEDIA_ADVERTISE_RTCP && !pjsua_var.media_cfg.ice_no_rtcp)
	++comp_cnt;

    status = pjmedia_ice_create3(pjsua_var.med_endpt, name, comp_cnt,
				 &ice_cfg, &ice_cb, 0, call_med,
				 &call_med->tp);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create ICE media transport",
		     status);
	goto on_error;
    }

    /* Wait until transport is initialized, or time out */
    if (!async) {
	pj_bool_t has_pjsua_lock = PJSUA_LOCK_IS_LOCKED();
        if (has_pjsua_lock)
	    PJSUA_UNLOCK();
        while (call_med->tp_ready == PJ_EPENDING) {
	    pjsua_handle_events(100);
        }
	if (has_pjsua_lock)
	    PJSUA_LOCK();
    }

    if (async && call_med->tp_ready == PJ_EPENDING) {
        return PJ_EPENDING;
    } else if (call_med->tp_ready != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error initializing ICE media transport",
		     call_med->tp_ready);
	status = call_med->tp_ready;
	goto on_error;
    }

    pjmedia_transport_simulate_lost(call_med->tp, PJMEDIA_DIR_ENCODING,
				    pjsua_var.media_cfg.tx_drop_pct);

    pjmedia_transport_simulate_lost(call_med->tp, PJMEDIA_DIR_DECODING,
				    pjsua_var.media_cfg.rx_drop_pct);

    return PJ_SUCCESS;

on_error:
    if (call_med->tp != NULL) {
	pjmedia_transport_close(call_med->tp);
	call_med->tp = NULL;
    }

    return status;
}

#if DISABLED_FOR_TICKET_1185
/* Create ICE media transports (when ice is enabled) */
static pj_status_t create_ice_media_transports(pjsua_transport_config *cfg)
{
    unsigned i;
    pj_status_t status;

    for (i=0; i < pjsua_var.ua_cfg.max_calls; ++i) {
	pjsua_call *call = &pjsua_var.calls[i];
	unsigned strm_idx;

	for (strm_idx=0; strm_idx < call->med_cnt; ++strm_idx) {
	    pjsua_call_media *call_med = &call->media[strm_idx];

	    status = create_ice_media_transport(cfg, call_med);
	    if (status != PJ_SUCCESS)
		goto on_error;
	}
    }

    return PJ_SUCCESS;

on_error:
    for (i=0; i < pjsua_var.ua_cfg.max_calls; ++i) {
	pjsua_call *call = &pjsua_var.calls[i];
	unsigned strm_idx;

	for (strm_idx=0; strm_idx < call->med_cnt; ++strm_idx) {
	    pjsua_call_media *call_med = &call->media[strm_idx];

	    if (call_med->tp) {
		pjmedia_transport_close(call_med->tp);
		call_med->tp = NULL;
	    }
	}
    }
    return status;
}
#endif

#if DISABLED_FOR_TICKET_1185
/*
 * Create media transports for all the calls. This function creates
 * one UDP media transport for each call.
 */
PJ_DEF(pj_status_t) pjsua_media_transports_create(
			const pjsua_transport_config *app_cfg)
{
    pjsua_transport_config cfg;
    unsigned i;
    pj_status_t status;


    /* Make sure pjsua_init() has been called */
    PJ_ASSERT_RETURN(pjsua_var.ua_cfg.max_calls>0, PJ_EINVALIDOP);

    PJSUA_LOCK();

    /* Delete existing media transports */
    for (i=0; i<pjsua_var.ua_cfg.max_calls; ++i) {
	pjsua_call *call = &pjsua_var.calls[i];
	unsigned strm_idx;

	for (strm_idx=0; strm_idx < call->med_cnt; ++strm_idx) {
	    pjsua_call_media *call_med = &call->media[strm_idx];

	    if (call_med->tp && call_med->tp_auto_del) {
		pjmedia_transport_close(call_med->tp);
		call_med->tp = NULL;
		call_med->tp_orig = NULL;
	    }
	}
    }

    /* Copy config */
    pjsua_transport_config_dup(pjsua_var.pool, &cfg, app_cfg);

    /* Create the transports */
    if (pjsua_var.media_cfg.enable_ice) {
	status = create_ice_media_transports(&cfg);
    } else {
	status = create_udp_media_transports(&cfg);
    }

    /* Set media transport auto_delete to True */
    for (i=0; i<pjsua_var.ua_cfg.max_calls; ++i) {
	pjsua_call *call = &pjsua_var.calls[i];
	unsigned strm_idx;

	for (strm_idx=0; strm_idx < call->med_cnt; ++strm_idx) {
	    pjsua_call_media *call_med = &call->media[strm_idx];

	    call_med->tp_auto_del = PJ_TRUE;
	}
    }

    PJSUA_UNLOCK();

    return status;
}

/*
 * Attach application's created media transports.
 */
PJ_DEF(pj_status_t) pjsua_media_transports_attach(pjsua_media_transport tp[],
						  unsigned count,
						  pj_bool_t auto_delete)
{
    unsigned i;

    PJ_ASSERT_RETURN(tp && count==pjsua_var.ua_cfg.max_calls, PJ_EINVAL);

    /* Assign the media transports */
    for (i=0; i<pjsua_var.ua_cfg.max_calls; ++i) {
	pjsua_call *call = &pjsua_var.calls[i];
	unsigned strm_idx;

	for (strm_idx=0; strm_idx < call->med_cnt; ++strm_idx) {
	    pjsua_call_media *call_med = &call->media[strm_idx];

	    if (call_med->tp && call_med->tp_auto_del) {
		pjmedia_transport_close(call_med->tp);
		call_med->tp = NULL;
		call_med->tp_orig = NULL;
	    }
	}

	PJ_TODO(remove_pjsua_media_transports_attach);

	call->media[0].tp = tp[i].transport;
	call->media[0].tp_auto_del = auto_delete;
    }

    return PJ_SUCCESS;
}
#endif

/* Go through the list of media in the SDP, find acceptable media, and
 * sort them based on the "quality" of the media, and store the indexes
 * in the specified array. Media with the best quality will be listed
 * first in the array. The quality factors considered currently is
 * encryption.
 */
static void sort_media(const pjmedia_sdp_session *sdp,
		       const pj_str_t *type,
		       pjmedia_srtp_use	use_srtp,
		       pj_uint8_t midx[],
		       unsigned *p_count,
		       unsigned *p_total_count)
{
    unsigned i;
    unsigned count = 0;
    int score[PJSUA_MAX_CALL_MEDIA];

    pj_assert(*p_count >= PJSUA_MAX_CALL_MEDIA);
    pj_assert(*p_total_count >= PJSUA_MAX_CALL_MEDIA);

    *p_count = 0;
    *p_total_count = 0;
    for (i=0; i<PJSUA_MAX_CALL_MEDIA; ++i)
	score[i] = 1;

    /* Score each media */
    for (i=0; i<sdp->media_count && count<PJSUA_MAX_CALL_MEDIA; ++i) {
	const pjmedia_sdp_media *m = sdp->media[i];
	const pjmedia_sdp_conn *c;

	/* Skip different media */
	if (pj_stricmp(&m->desc.media, type) != 0) {
	    score[count++] = -22000;
	    continue;
	}

	c = m->conn? m->conn : sdp->conn;

	/* Supported transports */
	if (pj_stricmp2(&m->desc.transport, "RTP/SAVP")==0) {
	    switch (use_srtp) {
	    case PJMEDIA_SRTP_MANDATORY:
	    case PJMEDIA_SRTP_OPTIONAL:
		++score[i];
		break;
	    case PJMEDIA_SRTP_DISABLED:
		//--score[i];
		score[i] -= 5;
		break;
	    }
	} else if (pj_stricmp2(&m->desc.transport, "RTP/AVP")==0) {
	    switch (use_srtp) {
	    case PJMEDIA_SRTP_MANDATORY:
		//--score[i];
		score[i] -= 5;
		break;
	    case PJMEDIA_SRTP_OPTIONAL:
		/* No change in score */
		break;
	    case PJMEDIA_SRTP_DISABLED:
		++score[i];
		break;
	    }
	} else {
	    score[i] -= 10;
	}

	/* Is media disabled? */
	if (m->desc.port == 0)
	    score[i] -= 10;

	/* Is media inactive? */
	if (pjmedia_sdp_media_find_attr2(m, "inactive", NULL) ||
	    pj_strcmp2(&c->addr, "0.0.0.0") == 0)
	{
	    //score[i] -= 10;
	    score[i] -= 1;
	}

	++count;
    }

    /* Created sorted list based on quality */
    for (i=0; i<count; ++i) {
	unsigned j;
	int best = 0;

	for (j=1; j<count; ++j) {
	    if (score[j] > score[best])
		best = j;
	}
	/* Don't put media with negative score, that media is unacceptable
	 * for us.
	 */
	midx[i] = (pj_uint8_t)best;
	if (score[best] >= 0)
	    (*p_count)++;
	if (score[best] > -22000)
	    (*p_total_count)++;

	score[best] = -22000;

    }
}

/* Callback to receive media events */
pj_status_t call_media_on_event(pjmedia_event *event,
                                void *user_data)
{
    pjsua_call_media *call_med = (pjsua_call_media*)user_data;
    pjsua_call *call = call_med->call;
    pj_status_t status = PJ_SUCCESS;
  
    switch(event->type) {
	case PJMEDIA_EVENT_KEYFRAME_MISSING:
	    if (call->opt.req_keyframe_method & PJSUA_VID_REQ_KEYFRAME_SIP_INFO)
	    {
		pj_timestamp now;

		pj_get_timestamp(&now);
		if (pj_elapsed_msec(&call_med->last_req_keyframe, &now) >=
		    PJSUA_VID_REQ_KEYFRAME_INTERVAL)
		{
		    pjsua_msg_data msg_data;
		    const pj_str_t SIP_INFO = {"INFO", 4};
		    const char *BODY_TYPE = "application/media_control+xml";
		    const char *BODY =
			"<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
			"<media_control><vc_primitive><to_encoder>"
			"<picture_fast_update/>"
			"</to_encoder></vc_primitive></media_control>";

		    PJ_LOG(4,(THIS_FILE, 
			      "Sending video keyframe request via SIP INFO"));

		    pjsua_msg_data_init(&msg_data);
		    pj_cstr(&msg_data.content_type, BODY_TYPE);
		    pj_cstr(&msg_data.msg_body, BODY);
		    status = pjsua_call_send_request(call->index, &SIP_INFO, 
						     &msg_data);
		    if (status != PJ_SUCCESS) {
			pj_perror(3, THIS_FILE, status,
				  "Failed requesting keyframe via SIP INFO");
		    } else {
			call_med->last_req_keyframe = now;
		    }
		}
	    }
	    break;

	default:
	    break;
    }

    if (pjsua_var.ua_cfg.cb.on_call_media_event && call) {
	(*pjsua_var.ua_cfg.cb.on_call_media_event)(call->index,
						   call_med->idx, event);
    }

    return status;
}

/* Set media transport state and notify the application via the callback. */
void pjsua_set_media_tp_state(pjsua_call_media *call_med,
                              pjsua_med_tp_st tp_st)
{
    if (pjsua_var.ua_cfg.cb.on_call_media_transport_state &&
        call_med->tp_st != tp_st)
    {
        pjsua_med_tp_state_info info;

        pj_bzero(&info, sizeof(info));
        info.med_idx = call_med->idx;
        info.state = tp_st;
        info.status = call_med->tp_ready;
	(*pjsua_var.ua_cfg.cb.on_call_media_transport_state)(
            call_med->call->index, &info);
    }

    call_med->tp_st = tp_st;
}

/* Callback to resume pjsua_call_media_init() after media transport
 * creation is completed.
 */
static pj_status_t call_media_init_cb(pjsua_call_media *call_med,
                                      pj_status_t status,
                                      int security_level,
                                      int *sip_err_code)
{
    pjsua_acc *acc = &pjsua_var.acc[call_med->call->acc_id];
    pjmedia_transport_info tpinfo;
    int err_code = 0;

    if (status != PJ_SUCCESS)
        goto on_return;

    if (call_med->tp_st == PJSUA_MED_TP_CREATING)
        pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_IDLE);

    if (!call_med->tp_orig &&
        pjsua_var.ua_cfg.cb.on_create_media_transport)
    {
        call_med->use_custom_med_tp = PJ_TRUE;
    } else
        call_med->use_custom_med_tp = PJ_FALSE;

#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
    /* This function may be called when SRTP transport already exists
     * (e.g: in re-invite, update), don't need to destroy/re-create.
     */
    if (!call_med->tp_orig) {
	pjmedia_srtp_setting srtp_opt;
	pjmedia_transport *srtp = NULL;

	/* Check if SRTP requires secure signaling */
	if (acc->cfg.use_srtp != PJMEDIA_SRTP_DISABLED) {
	    if (security_level < acc->cfg.srtp_secure_signaling) {
		err_code = PJSIP_SC_NOT_ACCEPTABLE;
		status = PJSIP_ESESSIONINSECURE;
		goto on_return;
	    }
	}

	/* Always create SRTP adapter */
	pjmedia_srtp_setting_default(&srtp_opt);
	srtp_opt.close_member_tp = PJ_TRUE;

	/* If media session has been ever established, let's use remote's 
	 * preference in SRTP usage policy, especially when it is stricter.
	 */
	if (call_med->rem_srtp_use > acc->cfg.use_srtp)
	    srtp_opt.use = call_med->rem_srtp_use;
	else
	    srtp_opt.use = acc->cfg.use_srtp;

	status = pjmedia_transport_srtp_create(pjsua_var.med_endpt,
					       call_med->tp,
					       &srtp_opt, &srtp);
	if (status != PJ_SUCCESS) {
	    err_code = PJSIP_SC_INTERNAL_SERVER_ERROR;
	    goto on_return;
	}

	/* Set SRTP as current media transport */
	call_med->tp_orig = call_med->tp;
	call_med->tp = srtp;
    }
#else
    call_med->tp_orig = call_med->tp;
    PJ_UNUSED_ARG(security_level);
#endif


    pjmedia_transport_info_init(&tpinfo);
    pjmedia_transport_get_info(call_med->tp, &tpinfo);

    pj_sockaddr_cp(&call_med->rtp_addr, &tpinfo.sock_info.rtp_addr_name);


on_return:
    if (status != PJ_SUCCESS && call_med->tp) {
	pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_NULL);
	pjmedia_transport_close(call_med->tp);
	call_med->tp = NULL;
    }

    if (sip_err_code)
        *sip_err_code = err_code;

    if (call_med->med_init_cb) {
        pjsua_med_tp_state_info info;

        pj_bzero(&info, sizeof(info));
        info.status = status;
        info.state = call_med->tp_st;
        info.med_idx = call_med->idx;
        info.sip_err_code = err_code;
        (*call_med->med_init_cb)(call_med->call->index, &info);
    }

    return status;
}

/* Initialize the media line */
pj_status_t pjsua_call_media_init(pjsua_call_media *call_med,
                                  pjmedia_type type,
				  const pjsua_transport_config *tcfg,
				  int security_level,
				  int *sip_err_code,
                                  pj_bool_t async,
                                  pjsua_med_tp_state_cb cb)
{
    pj_status_t status = PJ_SUCCESS;

    /*
     * Note: this function may be called when the media already exists
     * (e.g. in reinvites, updates, etc.)
     */
    call_med->type = type;

    /* Create the media transport for initial call. Here are the possible
     * media transport state and the action needed:
     * - PJSUA_MED_TP_NULL or call_med->tp==NULL, create one.
     * - PJSUA_MED_TP_RUNNING, do nothing.
     * - PJSUA_MED_TP_DISABLED, re-init (media_create(), etc). Currently,
     *   this won't happen as media_channel_update() will always clean up
     *   the unused transport of a disabled media.
     */
    if (call_med->tp == NULL) {
#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)
	/* While in initial call, set default video devices */
	if (type == PJMEDIA_TYPE_VIDEO) {
	    status = pjsua_vid_channel_init(call_med);
	    if (status != PJ_SUCCESS)
		return status;
	}
#endif

        pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_CREATING);

	if (pjsua_var.media_cfg.enable_ice) {
	    status = create_ice_media_transport(tcfg, call_med, async);
            if (async && status == PJ_EPENDING) {
	        /* We will resume call media initialization in the
	         * on_ice_complete() callback.
	         */
                call_med->med_create_cb = &call_media_init_cb;
                call_med->med_init_cb = cb;

	        return PJ_EPENDING;
	    }
	} else {
	    status = create_udp_media_transport(tcfg, call_med);
	}

        if (status != PJ_SUCCESS) {
	    PJ_PERROR(1,(THIS_FILE, status, "Error creating media transport"));
	    return status;
	}

        /* Media transport creation completed immediately, so 
         * we don't need to call the callback.
         */
        call_med->med_init_cb = NULL;

    } else if (call_med->tp_st == PJSUA_MED_TP_DISABLED) {
	/* Media is being reenabled. */
	//pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_IDLE);

	pj_assert(!"Currently no media transport reuse");
    }

    return call_media_init_cb(call_med, status, security_level,
                              sip_err_code);
}

/* Callback to resume pjsua_media_channel_init() after media transport
 * initialization is completed.
 */
static pj_status_t media_channel_init_cb(pjsua_call_id call_id,
                                         const pjsua_med_tp_state_info *info)
{
    pjsua_call *call = &pjsua_var.calls[call_id];
    pj_status_t status = (info? info->status : PJ_SUCCESS);
    unsigned mi;

    if (info) {
        pj_mutex_lock(call->med_ch_mutex);

        /* Set the callback to NULL to indicate that the async operation
         * has completed.
         */
        call->media_prov[info->med_idx].med_init_cb = NULL;

        /* In case of failure, save the information to be returned
         * by the last media transport to finish.
         */
        if (info->status != PJ_SUCCESS)
            pj_memcpy(&call->med_ch_info, info, sizeof(info));

        /* Check whether all the call's medias have finished calling their
         * callbacks.
         */
        for (mi=0; mi < call->med_prov_cnt; ++mi) {
            pjsua_call_media *call_med = &call->media_prov[mi];

            if (call_med->med_init_cb) {
                pj_mutex_unlock(call->med_ch_mutex);
                return PJ_SUCCESS;
            }

            if (call_med->tp_ready != PJ_SUCCESS)
                status = call_med->tp_ready;
        }

        /* OK, we are called by the last media transport finished. */
        pj_mutex_unlock(call->med_ch_mutex);
    }

    if (call->med_ch_mutex) {
        pj_mutex_destroy(call->med_ch_mutex);
        call->med_ch_mutex = NULL;
    }

    if (status != PJ_SUCCESS) {
	if (call->med_ch_info.status == PJ_SUCCESS) {
	    call->med_ch_info.status = status;
	    call->med_ch_info.sip_err_code = PJSIP_SC_TEMPORARILY_UNAVAILABLE;
	}
	pjsua_media_prov_clean_up(call_id);
        goto on_return;
    }

    /* Tell the media transport of a new offer/answer session */
    for (mi=0; mi < call->med_prov_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media_prov[mi];

	/* Note: tp may be NULL if this media line is disabled */
	if (call_med->tp && call_med->tp_st == PJSUA_MED_TP_IDLE) {
            pj_pool_t *tmp_pool = call->async_call.pool_prov;
            
            if (!tmp_pool) {
                tmp_pool = (call->inv? call->inv->pool_prov:
                            call->async_call.dlg->pool);
            }

            if (call_med->use_custom_med_tp) {
                unsigned custom_med_tp_flags = 0;

                /* Use custom media transport returned by the application */
                call_med->tp =
                    (*pjsua_var.ua_cfg.cb.on_create_media_transport)
                        (call_id, mi, call_med->tp,
                         custom_med_tp_flags);
                if (!call_med->tp) {
                    status =
                        PJSIP_ERRNO_FROM_SIP_STATUS(PJSIP_SC_TEMPORARILY_UNAVAILABLE);
                }
            }

            if (call_med->tp) {
                status = pjmedia_transport_media_create(
                             call_med->tp, tmp_pool,
                             0, call->async_call.rem_sdp, mi);
            }
	    if (status != PJ_SUCCESS) {
                call->med_ch_info.status = status;
                call->med_ch_info.med_idx = mi;
                call->med_ch_info.state = call_med->tp_st;
                call->med_ch_info.sip_err_code = PJSIP_SC_TEMPORARILY_UNAVAILABLE;
		pjsua_media_prov_clean_up(call_id);
		goto on_return;
	    }

	    pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_INIT);
	}
    }

    call->med_ch_info.status = PJ_SUCCESS;

on_return:
    if (call->med_ch_cb)
        (*call->med_ch_cb)(call->index, &call->med_ch_info);

    return status;
}


/* Clean up media transports in provisional media that is not used
 * by call media.
 */
void pjsua_media_prov_clean_up(pjsua_call_id call_id)
{
    pjsua_call *call = &pjsua_var.calls[call_id];
    unsigned i;

    for (i = 0; i < call->med_prov_cnt; ++i) {
	pjsua_call_media *call_med = &call->media_prov[i];
	unsigned j;
	pj_bool_t used = PJ_FALSE;

	if (call_med->tp == NULL)
	    continue;

	for (j = 0; j < call->med_cnt; ++j) {
	    if (call->media[j].tp == call_med->tp) {
		used = PJ_TRUE;
		break;
	    }
	}

	if (!used) {
	    if (call_med->tp_st > PJSUA_MED_TP_IDLE) {
		pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_IDLE);
		pjmedia_transport_media_stop(call_med->tp);
	    }
	    pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_NULL);
	    pjmedia_transport_close(call_med->tp);
	    call_med->tp = call_med->tp_orig = NULL;
	}
    }
}


pj_status_t pjsua_media_channel_init(pjsua_call_id call_id,
				     pjsip_role_e role,
				     int security_level,
				     pj_pool_t *tmp_pool,
				     const pjmedia_sdp_session *rem_sdp,
				     int *sip_err_code,
                                     pj_bool_t async,
                                     pjsua_med_tp_state_cb cb)
{
    const pj_str_t STR_AUDIO = { "audio", 5 };
    const pj_str_t STR_VIDEO = { "video", 5 };
    pjsua_call *call = &pjsua_var.calls[call_id];
    pjsua_acc *acc = &pjsua_var.acc[call->acc_id];
    pj_uint8_t maudidx[PJSUA_MAX_CALL_MEDIA];
    unsigned maudcnt = PJ_ARRAY_SIZE(maudidx);
    unsigned mtotaudcnt = PJ_ARRAY_SIZE(maudidx);
    pj_uint8_t mvididx[PJSUA_MAX_CALL_MEDIA];
    unsigned mvidcnt = PJ_ARRAY_SIZE(mvididx);
    unsigned mtotvidcnt = PJ_ARRAY_SIZE(mvididx);
    unsigned mi;
    pj_bool_t pending_med_tp = PJ_FALSE;
    pj_bool_t reinit = PJ_FALSE;
    pj_status_t status;

    PJ_UNUSED_ARG(role);

    /*
     * Note: this function may be called when the media already exists
     * (e.g. in reinvites, updates, etc).
     */

    if (pjsua_get_state() != PJSUA_STATE_RUNNING)
	return PJ_EBUSY;

    if (async) {
        pj_pool_t *tmppool = (call->inv? call->inv->pool_prov:
                              call->async_call.dlg->pool);

        status = pj_mutex_create_simple(tmppool, NULL, &call->med_ch_mutex);
        if (status != PJ_SUCCESS)
            return status;
    }

    if (call->inv && call->inv->state == PJSIP_INV_STATE_CONFIRMED)
	reinit = PJ_TRUE;

    PJ_LOG(4,(THIS_FILE, "Call %d: %sinitializing media..",
			 call_id, (reinit?"re-":"") ));

    pj_log_push_indent();

    /* Init provisional media state */
    if (call->med_cnt == 0) {
	/* New media session, just copy whole from call media state. */
	pj_memcpy(call->media_prov, call->media, sizeof(call->media));
    } else {
	/* Clean up any unused transports. Note that when local SDP reoffer
	 * is rejected by remote, there may be any initialized transports that
	 * are not used by call media and currently there is no notification
	 * from PJSIP level regarding the reoffer rejection.
	 */
	pjsua_media_prov_clean_up(call_id);

	/* Updating media session, copy from call media state. */
	pj_memcpy(call->media_prov, call->media,
		  sizeof(call->media[0]) * call->med_cnt);
    }
    call->med_prov_cnt = call->med_cnt;

#if DISABLED_FOR_TICKET_1185
    /* Return error if media transport has not been created yet
     * (e.g. application is starting)
     */
    for (i=0; i<call->med_cnt; ++i) {
	if (call->media[i].tp == NULL) {
	    status = PJ_EBUSY;
	    goto on_error;
	}
    }
#endif

    /* Get media count for each media type */
    if (rem_sdp) {
	sort_media(rem_sdp, &STR_AUDIO, acc->cfg.use_srtp,
		   maudidx, &maudcnt, &mtotaudcnt);
	if (maudcnt==0) {
	    /* Expecting audio in the offer */
	    if (sip_err_code) *sip_err_code = PJSIP_SC_NOT_ACCEPTABLE_HERE;
	    status = PJSIP_ERRNO_FROM_SIP_STATUS(PJSIP_SC_NOT_ACCEPTABLE_HERE);
	    goto on_error;
	}

#if PJMEDIA_HAS_VIDEO
	sort_media(rem_sdp, &STR_VIDEO, acc->cfg.use_srtp,
		   mvididx, &mvidcnt, &mtotvidcnt);
#else
	mvidcnt = mtotvidcnt = 0;
	PJ_UNUSED_ARG(STR_VIDEO);
#endif

	/* Update media count only when remote add any media, this media count
	 * must never decrease. Also note that we shouldn't apply the media
	 * count setting (of the call setting) before the SDP negotiation.
	 */
	if (call->med_prov_cnt < rem_sdp->media_count)
	    call->med_prov_cnt = PJ_MIN(rem_sdp->media_count,
					PJSUA_MAX_CALL_MEDIA);

	call->rem_offerer = PJ_TRUE;
	call->rem_aud_cnt = maudcnt;
	call->rem_vid_cnt = mvidcnt;

    } else {

	/* If call already established, calculate media count from current 
	 * local active SDP and call setting. Otherwise, calculate media
	 * count from the call setting only.
	 */
	if (reinit) {
	    const pjmedia_sdp_session *sdp;

	    status = pjmedia_sdp_neg_get_active_local(call->inv->neg, &sdp);
	    pj_assert(status == PJ_SUCCESS);

	    sort_media(sdp, &STR_AUDIO, acc->cfg.use_srtp,
		       maudidx, &maudcnt, &mtotaudcnt);
	    pj_assert(maudcnt > 0);

	    sort_media(sdp, &STR_VIDEO, acc->cfg.use_srtp,
		       mvididx, &mvidcnt, &mtotvidcnt);

	    /* Call setting may add or remove media. Adding media is done by
	     * enabling any disabled/port-zeroed media first, then adding new
	     * media whenever needed. Removing media is done by disabling
	     * media with the lowest 'quality'.
	     */

	    /* Check if we need to add new audio */
	    if (maudcnt < call->opt.aud_cnt &&
		mtotaudcnt < call->opt.aud_cnt)
	    {
		for (mi = 0; mi < call->opt.aud_cnt - mtotaudcnt; ++mi)
		    maudidx[maudcnt++] = (pj_uint8_t)call->med_prov_cnt++;
		
		mtotaudcnt = call->opt.aud_cnt;
	    }
	    maudcnt = call->opt.aud_cnt;

	    /* Check if we need to add new video */
	    if (mvidcnt < call->opt.vid_cnt &&
		mtotvidcnt < call->opt.vid_cnt)
	    {
		for (mi = 0; mi < call->opt.vid_cnt - mtotvidcnt; ++mi)
		    mvididx[mvidcnt++] = (pj_uint8_t)call->med_prov_cnt++;

		mtotvidcnt = call->opt.vid_cnt;
	    }
	    mvidcnt = call->opt.vid_cnt;

	} else {

	    maudcnt = mtotaudcnt = call->opt.aud_cnt;
	    for (mi=0; mi<maudcnt; ++mi) {
		maudidx[mi] = (pj_uint8_t)mi;
	    }
	    mvidcnt = mtotvidcnt = call->opt.vid_cnt;
	    for (mi=0; mi<mvidcnt; ++mi) {
		mvididx[mi] = (pj_uint8_t)(maudcnt + mi);
	    }
	    call->med_prov_cnt = maudcnt + mvidcnt;

	    /* Need to publish supported media? */
	    if (call->opt.flag & PJSUA_CALL_INCLUDE_DISABLED_MEDIA) {
		if (mtotaudcnt == 0) {
		    mtotaudcnt = 1;
		    maudidx[0] = (pj_uint8_t)call->med_prov_cnt++;
		}
#if PJMEDIA_HAS_VIDEO
		if (mtotvidcnt == 0) {
		    mtotvidcnt = 1;
		    mvididx[0] = (pj_uint8_t)call->med_prov_cnt++;
		}
#endif
	    }
	}

	call->rem_offerer = PJ_FALSE;
    }

    if (call->med_prov_cnt == 0) {
	/* Expecting at least one media */
	if (sip_err_code) *sip_err_code = PJSIP_SC_NOT_ACCEPTABLE_HERE;
	status = PJSIP_ERRNO_FROM_SIP_STATUS(PJSIP_SC_NOT_ACCEPTABLE_HERE);
	goto on_error;
    }

    if (async) {
        call->med_ch_cb = cb;
    }

    if (rem_sdp) {
        call->async_call.rem_sdp =
            pjmedia_sdp_session_clone(call->inv->pool_prov, rem_sdp);
    } else {
	call->async_call.rem_sdp = NULL;
    }

    call->async_call.pool_prov = tmp_pool;

    /* Initialize each media line */
    for (mi=0; mi < call->med_prov_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media_prov[mi];
	pj_bool_t enabled = PJ_FALSE;
	pjmedia_type media_type = PJMEDIA_TYPE_UNKNOWN;

	if (pj_memchr(maudidx, mi, mtotaudcnt * sizeof(maudidx[0]))) {
	    media_type = PJMEDIA_TYPE_AUDIO;
	    if (call->opt.aud_cnt &&
		pj_memchr(maudidx, mi, maudcnt * sizeof(maudidx[0])))
	    {
		enabled = PJ_TRUE;
	    }
	} else if (pj_memchr(mvididx, mi, mtotvidcnt * sizeof(mvididx[0]))) {
	    media_type = PJMEDIA_TYPE_VIDEO;
	    if (call->opt.vid_cnt &&
		pj_memchr(mvididx, mi, mvidcnt * sizeof(mvididx[0])))
	    {
		enabled = PJ_TRUE;
	    }
	}

	if (enabled) {
	    status = pjsua_call_media_init(call_med, media_type,
	                                   &acc->cfg.rtp_cfg,
					   security_level, sip_err_code,
                                           async,
                                           (async? &media_channel_init_cb:
                                            NULL));
            if (status == PJ_EPENDING) {
                pending_med_tp = PJ_TRUE;
            } else if (status != PJ_SUCCESS) {
                if (pending_med_tp) {
                    /* Save failure information. */
                    call_med->tp_ready = status;
                    pj_bzero(&call->med_ch_info, sizeof(call->med_ch_info));
                    call->med_ch_info.status = status;
                    call->med_ch_info.state = call_med->tp_st;
                    call->med_ch_info.med_idx = call_med->idx;
                    if (sip_err_code)
                        call->med_ch_info.sip_err_code = *sip_err_code;

                    /* We will return failure in the callback later. */
                    return PJ_EPENDING;
                }

                pjsua_media_prov_clean_up(call_id);
		goto on_error;
	    }
	} else {
	    /* By convention, the media is disabled if transport is NULL 
	     * or transport state is PJSUA_MED_TP_DISABLED.
	     */
	    if (call_med->tp) {
		// Don't close transport here, as SDP negotiation has not been
		// done and stream may be still active. Once SDP negotiation
		// is done (channel_update() invoked), this transport will be
		// closed there.
		//pjmedia_transport_close(call_med->tp);
		//call_med->tp = NULL;
		pj_assert(call_med->tp_st == PJSUA_MED_TP_INIT || 
			  call_med->tp_st == PJSUA_MED_TP_RUNNING);
		pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_DISABLED);
	    }

	    /* Put media type just for info */
	    call_med->type = media_type;
	}
    }

    call->audio_idx = maudidx[0];

    PJ_LOG(4,(THIS_FILE, "Media index %d selected for audio call %d",
	      call->audio_idx, call->index));

    if (pending_med_tp) {
        /* We shouldn't use temporary pool anymore. */
        call->async_call.pool_prov = NULL;
        /* We have a pending media transport initialization. */
        pj_log_pop_indent();
        return PJ_EPENDING;
    }

    /* Media transport initialization completed immediately, so 
     * we don't need to call the callback.
     */
    call->med_ch_cb = NULL;

    status = media_channel_init_cb(call_id, NULL);
    if (status != PJ_SUCCESS && sip_err_code)
        *sip_err_code = call->med_ch_info.sip_err_code;

    pj_log_pop_indent();
    return status;

on_error:
    if (call->med_ch_mutex) {
        pj_mutex_destroy(call->med_ch_mutex);
        call->med_ch_mutex = NULL;
    }

    pj_log_pop_indent();
    return status;
}


/* Create SDP based on the current media channel. Note that, this function
 * will not modify the media channel, so when receiving new offer or
 * updating media count (via call setting), media channel must be reinit'd
 * (using pjsua_media_channel_init()) first before calling this function.
 */
pj_status_t pjsua_media_channel_create_sdp(pjsua_call_id call_id, 
					   pj_pool_t *pool,
					   const pjmedia_sdp_session *rem_sdp,
					   pjmedia_sdp_session **p_sdp,
					   int *sip_err_code)
{
    enum { MAX_MEDIA = PJSUA_MAX_CALL_MEDIA };
    pjmedia_sdp_session *sdp;
    pj_sockaddr origin;
    pjsua_call *call = &pjsua_var.calls[call_id];
    pjmedia_sdp_neg_state sdp_neg_state = PJMEDIA_SDP_NEG_STATE_NULL;
    unsigned mi;
    unsigned tot_bandw_tias = 0;
    pj_status_t status;

    if (pjsua_get_state() != PJSUA_STATE_RUNNING)
	return PJ_EBUSY;

#if 0
    // This function should not really change the media channel.
    if (rem_sdp) {
	/* If this is a re-offer, let's re-initialize media as remote may
	 * add or remove media
	 */
	if (call->inv && call->inv->state == PJSIP_INV_STATE_CONFIRMED) {
	    status = pjsua_media_channel_init(call_id, PJSIP_ROLE_UAS,
					      call->secure_level, pool,
					      rem_sdp, sip_err_code,
                                              PJ_FALSE, NULL);
	    if (status != PJ_SUCCESS)
		return status;
	}
    } else {
	/* Audio is first in our offer, by convention */
	// The audio_idx should not be changed here, as this function may be
	// called in generating re-offer and the current active audio index
	// can be anywhere.
	//call->audio_idx = 0;
    }
#endif

#if 0
    // Since r3512, old-style hold should have got transport, created by 
    // pjsua_media_channel_init() in initial offer/answer or remote reoffer.
    /* Create media if it's not created. This could happen when call is
     * currently on-hold (with the old style hold)
     */
    if (call->media[call->audio_idx].tp == NULL) {
	pjsip_role_e role;
	role = (rem_sdp ? PJSIP_ROLE_UAS : PJSIP_ROLE_UAC);
	status = pjsua_media_channel_init(call_id, role, call->secure_level, 
					  pool, rem_sdp, sip_err_code);
	if (status != PJ_SUCCESS)
	    return status;
    }
#endif

    /* Get SDP negotiator state */
    if (call->inv && call->inv->neg)
	sdp_neg_state = pjmedia_sdp_neg_get_state(call->inv->neg);

    /* Get one address to use in the origin field */
    pj_bzero(&origin, sizeof(origin));
    for (mi=0; mi<call->med_prov_cnt; ++mi) {
	pjmedia_transport_info tpinfo;

	if (call->media_prov[mi].tp == NULL)
	    continue;

	pjmedia_transport_info_init(&tpinfo);
	pjmedia_transport_get_info(call->media_prov[mi].tp, &tpinfo);
	pj_sockaddr_cp(&origin, &tpinfo.sock_info.rtp_addr_name);
	break;
    }

    /* Create the base (blank) SDP */
    status = pjmedia_endpt_create_base_sdp(pjsua_var.med_endpt, pool, NULL,
                                           &origin, &sdp);
    if (status != PJ_SUCCESS)
	return status;

    /* Process each media line */
    for (mi=0; mi<call->med_prov_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media_prov[mi];
	pjmedia_sdp_media *m = NULL;
	pjmedia_transport_info tpinfo;
	unsigned i;

	if (rem_sdp && mi >= rem_sdp->media_count) {
	    /* Remote might have removed some media lines. */
	    break;
	}

	if (call_med->tp == NULL || call_med->tp_st == PJSUA_MED_TP_DISABLED)
	{
	    /*
	     * This media is disabled. Just create a valid SDP with zero
	     * port.
	     */
	    if (rem_sdp) {
		/* Just clone the remote media and deactivate it */
		m = pjmedia_sdp_media_clone_deactivate(pool,
						       rem_sdp->media[mi]);
	    } else {
		m = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_media);
		m->desc.transport = pj_str("RTP/AVP");
		m->desc.fmt_count = 1;
		m->conn = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_conn);
		m->conn->net_type = pj_str("IN");
		m->conn->addr_type = pj_str("IP4");
		m->conn->addr = pj_str("127.0.0.1");

		switch (call_med->type) {
		case PJMEDIA_TYPE_AUDIO:
		    m->desc.media = pj_str("audio");
		    m->desc.fmt[0] = pj_str("0");
		    break;
		case PJMEDIA_TYPE_VIDEO:
		    m->desc.media = pj_str("video");
		    m->desc.fmt[0] = pj_str("31");
		    break;
		default:
		    /* This must be us generating re-offer, and some unknown
		     * media may exist, so just clone from active local SDP
		     * (and it should have been deactivated already).
		     */
		    pj_assert(call->inv && call->inv->neg &&
			      sdp_neg_state == PJMEDIA_SDP_NEG_STATE_DONE);
		    {
			const pjmedia_sdp_session *s_;
			pjmedia_sdp_neg_get_active_local(call->inv->neg, &s_);

			pj_assert(mi < s_->media_count);
			m = pjmedia_sdp_media_clone(pool, s_->media[mi]);
			m->desc.port = 0;
		    }
		    break;
		}
	    }

	    sdp->media[sdp->media_count++] = m;
	    continue;
	}

	/* Get transport address info */
	pjmedia_transport_info_init(&tpinfo);
	pjmedia_transport_get_info(call_med->tp, &tpinfo);

	/* Ask pjmedia endpoint to create SDP media line */
	switch (call_med->type) {
	case PJMEDIA_TYPE_AUDIO:
	    status = pjmedia_endpt_create_audio_sdp(pjsua_var.med_endpt, pool,
                                                    &tpinfo.sock_info, 0, &m);
	    break;
#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)
	case PJMEDIA_TYPE_VIDEO:
	    status = pjmedia_endpt_create_video_sdp(pjsua_var.med_endpt, pool,
	                                            &tpinfo.sock_info, 0, &m);
	    break;
#endif
	default:
	    pj_assert(!"Invalid call_med media type");
	    return PJ_EBUG;
	}

	if (status != PJ_SUCCESS)
	    return status;

	sdp->media[sdp->media_count++] = m;

	/* Give to transport */
	status = pjmedia_transport_encode_sdp(call_med->tp, pool,
					      sdp, rem_sdp, mi);
	if (status != PJ_SUCCESS) {
	    if (sip_err_code) *sip_err_code = PJSIP_SC_NOT_ACCEPTABLE;
	    return status;
	}

	/* Copy c= line of the first media to session level,
	 * if there's none.
	 */
	if (sdp->conn == NULL) {
	    sdp->conn = pjmedia_sdp_conn_clone(pool, m->conn);
	}

	
	/* Find media bandwidth info */
	for (i = 0; i < m->bandw_count; ++i) {
	    const pj_str_t STR_BANDW_MODIFIER_TIAS = { "TIAS", 4 };
	    if (!pj_stricmp(&m->bandw[i]->modifier, &STR_BANDW_MODIFIER_TIAS))
	    {
		tot_bandw_tias += m->bandw[i]->value;
		break;
	    }
	}
    }

    /* Add NAT info in the SDP */
    if (pjsua_var.ua_cfg.nat_type_in_sdp) {
	pjmedia_sdp_attr *a;
	pj_str_t value;
	char nat_info[80];

	value.ptr = nat_info;
	if (pjsua_var.ua_cfg.nat_type_in_sdp == 1) {
	    value.slen = pj_ansi_snprintf(nat_info, sizeof(nat_info),
					  "%d", pjsua_var.nat_type);
	} else {
	    const char *type_name = pj_stun_get_nat_name(pjsua_var.nat_type);
	    value.slen = pj_ansi_snprintf(nat_info, sizeof(nat_info),
					  "%d %s",
					  pjsua_var.nat_type,
					  type_name);
	}

	a = pjmedia_sdp_attr_create(pool, "X-nat", &value);

	pjmedia_sdp_attr_add(&sdp->attr_count, sdp->attr, a);

    }


    /* Add bandwidth info in session level using bandwidth modifier "AS". */
    if (tot_bandw_tias) {
	unsigned bandw;
	const pj_str_t STR_BANDW_MODIFIER_AS = { "AS", 2 };
	pjmedia_sdp_bandw *b;

	/* AS bandwidth = RTP bitrate + RTCP bitrate.
	 * RTP bitrate  = payload bitrate (total TIAS) + overheads (~16kbps).
	 * RTCP bitrate = est. 5% of RTP bitrate.
	 * Note that AS bandwidth is in kbps.
	 */
	bandw = tot_bandw_tias + 16000;
	bandw += bandw * 5 / 100;
	b = PJ_POOL_ALLOC_T(pool, pjmedia_sdp_bandw);
	b->modifier = STR_BANDW_MODIFIER_AS;
	b->value = bandw / 1000;
	sdp->bandw[sdp->bandw_count++] = b;
    }


#if DISABLED_FOR_TICKET_1185 && defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
    /* Check if SRTP is in optional mode and configured to use duplicated
     * media, i.e: secured and unsecured version, in the SDP offer.
     */
    if (!rem_sdp &&
	pjsua_var.acc[call->acc_id].cfg.use_srtp == PJMEDIA_SRTP_OPTIONAL &&
	pjsua_var.acc[call->acc_id].cfg.srtp_optional_dup_offer)
    {
	unsigned i;

	for (i = 0; i < sdp->media_count; ++i) {
	    pjmedia_sdp_media *m = sdp->media[i];

	    /* Check if this media is unsecured but has SDP "crypto"
	     * attribute.
	     */
	    if (pj_stricmp2(&m->desc.transport, "RTP/AVP") == 0 &&
		pjmedia_sdp_media_find_attr2(m, "crypto", NULL) != NULL)
	    {
		if (i == (unsigned)call->audio_idx &&
		    sdp_neg_state == PJMEDIA_SDP_NEG_STATE_DONE)
		{
		    /* This is a session update, and peer has chosen the
		     * unsecured version, so let's make this unsecured too.
		     */
		    pjmedia_sdp_media_remove_all_attr(m, "crypto");
		} else {
		    /* This is new offer, duplicate media so we'll have
		     * secured (with "RTP/SAVP" transport) and and unsecured
		     * versions.
		     */
		    pjmedia_sdp_media *new_m;

		    /* Duplicate this media and apply secured transport */
		    new_m = pjmedia_sdp_media_clone(pool, m);
		    pj_strdup2(pool, &new_m->desc.transport, "RTP/SAVP");

		    /* Remove the "crypto" attribute in the unsecured media */
		    pjmedia_sdp_media_remove_all_attr(m, "crypto");

		    /* Insert the new media before the unsecured media */
		    if (sdp->media_count < PJMEDIA_MAX_SDP_MEDIA) {
			pj_array_insert(sdp->media, sizeof(new_m),
					sdp->media_count, i, &new_m);
			++sdp->media_count;
			++i;
		    }
		}
	    }
	}
    }
#endif

    call->rem_offerer = (rem_sdp != NULL);

    /* Notify application */
    if (pjsua_var.ua_cfg.cb.on_call_sdp_created) {
	(*pjsua_var.ua_cfg.cb.on_call_sdp_created)(call_id, sdp,
						   pool, rem_sdp);
    }

    *p_sdp = sdp;
    return PJ_SUCCESS;
}


static void stop_media_session(pjsua_call_id call_id)
{
    pjsua_call *call = &pjsua_var.calls[call_id];
    unsigned mi;

    pj_log_push_indent();

    for (mi=0; mi<call->med_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media[mi];

	if (call_med->type == PJMEDIA_TYPE_AUDIO) {
	    pjsua_aud_stop_stream(call_med);
	}

#if PJMEDIA_HAS_VIDEO
	else if (call_med->type == PJMEDIA_TYPE_VIDEO) {
	    pjsua_vid_stop_stream(call_med);
	}
#endif

	PJ_LOG(4,(THIS_FILE, "Media session call%02d:%d is destroyed",
			     call_id, mi));
        call_med->prev_state = call_med->state;
	call_med->state = PJSUA_CALL_MEDIA_NONE;

	/* Try to sync recent changes to provisional media */
	if (mi<call->med_prov_cnt && call->media_prov[mi].tp==call_med->tp)
	{
	    pjsua_call_media *prov_med = &call->media_prov[mi];

	    /* Media state */
	    prov_med->prev_state = call_med->prev_state;
	    prov_med->state	 = call_med->state;

	    /* RTP seq/ts */
	    prov_med->rtp_tx_seq_ts_set = call_med->rtp_tx_seq_ts_set;
	    prov_med->rtp_tx_seq	= call_med->rtp_tx_seq;
	    prov_med->rtp_tx_ts		= call_med->rtp_tx_ts;

	    /* Stream */
	    if (call_med->type == PJMEDIA_TYPE_AUDIO) {
		prov_med->strm.a.conf_slot = call_med->strm.a.conf_slot;
		prov_med->strm.a.stream	   = call_med->strm.a.stream;
	    }
#if PJMEDIA_HAS_VIDEO
	    else if (call_med->type == PJMEDIA_TYPE_VIDEO) {
		prov_med->strm.v.cap_win_id = call_med->strm.v.cap_win_id;
		prov_med->strm.v.rdr_win_id = call_med->strm.v.rdr_win_id;
		prov_med->strm.v.stream	    = call_med->strm.v.stream;
	    }
#endif
	}
    }

    pj_log_pop_indent();
}

pj_status_t pjsua_media_channel_deinit(pjsua_call_id call_id)
{
    pjsua_call *call = &pjsua_var.calls[call_id];
    unsigned mi;

    PJSUA_LOCK();
    for (mi=0; mi<call->med_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media[mi];

        if (call_med->tp_st == PJSUA_MED_TP_CREATING) {
            /* We will do the deinitialization after media transport
             * creation is completed.
             */
            call->async_call.med_ch_deinit = PJ_TRUE;
            PJSUA_UNLOCK();
            return PJ_SUCCESS;
        }
    }
    PJSUA_UNLOCK();

    PJ_LOG(4,(THIS_FILE, "Call %d: deinitializing media..", call_id));
    pj_log_push_indent();

    stop_media_session(call_id);

    /* Clean up media transports */
    pjsua_media_prov_clean_up(call_id);
    call->med_prov_cnt = 0;
    for (mi=0; mi<call->med_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media[mi];

        if (call_med->tp_st > PJSUA_MED_TP_IDLE) {
	    pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_IDLE);
	    pjmedia_transport_media_stop(call_med->tp);
	}

	if (call_med->tp) {
	    pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_NULL);
	    pjmedia_transport_close(call_med->tp);
	    call_med->tp = call_med->tp_orig = NULL;
	}
        call_med->tp_orig = NULL;
    }

    pj_log_pop_indent();

    return PJ_SUCCESS;
}


pj_status_t pjsua_media_channel_update(pjsua_call_id call_id,
				       const pjmedia_sdp_session *local_sdp,
				       const pjmedia_sdp_session *remote_sdp)
{
    pjsua_call *call = &pjsua_var.calls[call_id];
    pjsua_acc *acc = &pjsua_var.acc[call->acc_id];
    pj_pool_t *tmp_pool = call->inv->pool_prov;
    unsigned mi;
    pj_bool_t got_media = PJ_FALSE;
    pj_status_t status = PJ_SUCCESS;

    const pj_str_t STR_AUDIO = { "audio", 5 };
    const pj_str_t STR_VIDEO = { "video", 5 };
    pj_uint8_t maudidx[PJSUA_MAX_CALL_MEDIA];
    unsigned maudcnt = PJ_ARRAY_SIZE(maudidx);
    unsigned mtotaudcnt = PJ_ARRAY_SIZE(maudidx);
    pj_uint8_t mvididx[PJSUA_MAX_CALL_MEDIA];
    unsigned mvidcnt = PJ_ARRAY_SIZE(mvididx);
    unsigned mtotvidcnt = PJ_ARRAY_SIZE(mvididx);
    pj_bool_t need_renego_sdp = PJ_FALSE;

    if (pjsua_get_state() != PJSUA_STATE_RUNNING)
	return PJ_EBUSY;

    PJ_LOG(4,(THIS_FILE, "Call %d: updating media..", call_id));
    pj_log_push_indent();

    /* Destroy existing media session, if any. */
    stop_media_session(call->index);

    /* Call media count must be at least equal to SDP media. Note that
     * it may not be equal when remote removed any SDP media line.
     */
    pj_assert(call->med_prov_cnt >= local_sdp->media_count);

    /* Reset audio_idx first */
    call->audio_idx = -1;

    /* Sort audio/video based on "quality" */
    sort_media(local_sdp, &STR_AUDIO, acc->cfg.use_srtp,
	       maudidx, &maudcnt, &mtotaudcnt);
#if PJMEDIA_HAS_VIDEO
    sort_media(local_sdp, &STR_VIDEO, acc->cfg.use_srtp,
	       mvididx, &mvidcnt, &mtotvidcnt);
#else
    PJ_UNUSED_ARG(STR_VIDEO);
    mvidcnt = mtotvidcnt = 0;
#endif

    /* Applying media count limitation. Note that in generating SDP answer,
     * no media count limitation applied, as we didn't know yet which media
     * would pass the SDP negotiation.
     */
    if (maudcnt > call->opt.aud_cnt || mvidcnt > call->opt.vid_cnt)
    {
	pjmedia_sdp_session *local_sdp2;

	maudcnt = PJ_MIN(maudcnt, call->opt.aud_cnt);
	mvidcnt = PJ_MIN(mvidcnt, call->opt.vid_cnt);
	local_sdp2 = pjmedia_sdp_session_clone(tmp_pool, local_sdp);

	for (mi=0; mi < local_sdp2->media_count; ++mi) {
	    pjmedia_sdp_media *m = local_sdp2->media[mi];

	    if (m->desc.port == 0 ||
		pj_memchr(maudidx, mi, maudcnt*sizeof(maudidx[0])) ||
		pj_memchr(mvididx, mi, mvidcnt*sizeof(mvididx[0])))
	    {
		continue;
	    }
	    
	    /* Deactivate this media */
	    pjmedia_sdp_media_deactivate(tmp_pool, m);
	}

	local_sdp = local_sdp2;
	need_renego_sdp = PJ_TRUE;
    }

    /* Process each media stream */
    for (mi=0; mi < call->med_prov_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media_prov[mi];

	if (mi >= local_sdp->media_count ||
	    mi >= remote_sdp->media_count)
	{
	    /* This may happen when remote removed any SDP media lines in
	     * its re-offer.
	     */
	    if (call_med->tp) {
		/* Close the media transport */
		pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_NULL);
		pjmedia_transport_close(call_med->tp);
		call_med->tp = call_med->tp_orig = NULL;
	    }
	    continue;
#if 0
	    /* Something is wrong */
	    PJ_LOG(1,(THIS_FILE, "Error updating media for call %d: "
		      "invalid media index %d in SDP", call_id, mi));
	    status = PJMEDIA_SDP_EINSDP;
	    goto on_error;
#endif
	}

	if (call_med->type==PJMEDIA_TYPE_AUDIO) {
	    pjmedia_stream_info the_si, *si = &the_si;

	    status = pjmedia_stream_info_from_sdp(si, tmp_pool, pjsua_var.med_endpt,
	                                          local_sdp, remote_sdp, mi);
	    if (status != PJ_SUCCESS) {
		PJ_PERROR(1,(THIS_FILE, status,
			     "pjmedia_stream_info_from_sdp() failed "
			         "for call_id %d media %d",
			     call_id, mi));
		continue;
	    }

	    /* Check if no media is active */
	    if (si->dir == PJMEDIA_DIR_NONE) {
		/* Update call media state and direction */
		call_med->state = PJSUA_CALL_MEDIA_NONE;
		call_med->dir = PJMEDIA_DIR_NONE;

	    } else {
		pjmedia_transport_info tp_info;

		/* Start/restart media transport based on info in SDP */
		status = pjmedia_transport_media_start(call_med->tp,
						       tmp_pool, local_sdp,
						       remote_sdp, mi);
		if (status != PJ_SUCCESS) {
		    PJ_PERROR(1,(THIS_FILE, status,
				 "pjmedia_transport_media_start() failed "
				     "for call_id %d media %d",
				 call_id, mi));
		    continue;
		}

		pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_RUNNING);

		/* Get remote SRTP usage policy */
		pjmedia_transport_info_init(&tp_info);
		pjmedia_transport_get_info(call_med->tp, &tp_info);
		if (tp_info.specific_info_cnt > 0) {
		    unsigned i;
		    for (i = 0; i < tp_info.specific_info_cnt; ++i) {
			if (tp_info.spc_info[i].type == PJMEDIA_TRANSPORT_TYPE_SRTP)
			{
			    pjmedia_srtp_info *srtp_info =
					(pjmedia_srtp_info*) tp_info.spc_info[i].buffer;

			    call_med->rem_srtp_use = srtp_info->peer_use;
			    break;
			}
		    }
		}

		/* Call media direction */
		call_med->dir = si->dir;

		/* Call media state */
		if (call->local_hold)
		    call_med->state = PJSUA_CALL_MEDIA_LOCAL_HOLD;
		else if (call_med->dir == PJMEDIA_DIR_DECODING)
		    call_med->state = PJSUA_CALL_MEDIA_REMOTE_HOLD;
		else
		    call_med->state = PJSUA_CALL_MEDIA_ACTIVE;
	    }

	    /* Call implementation */
	    status = pjsua_aud_channel_update(call_med, tmp_pool, si,
	                                      local_sdp, remote_sdp);
	    if (status != PJ_SUCCESS) {
		PJ_PERROR(1,(THIS_FILE, status,
			     "pjsua_aud_channel_update() failed "
				 "for call_id %d media %d",
			     call_id, mi));
		continue;
	    }

	    /* Print info. */
	    if (status == PJ_SUCCESS) {
		char info[80];
		int info_len = 0;
		int len;
		const char *dir;

		switch (si->dir) {
		case PJMEDIA_DIR_NONE:
		    dir = "inactive";
		    break;
		case PJMEDIA_DIR_ENCODING:
		    dir = "sendonly";
		    break;
		case PJMEDIA_DIR_DECODING:
		    dir = "recvonly";
		    break;
		case PJMEDIA_DIR_ENCODING_DECODING:
		    dir = "sendrecv";
		    break;
		default:
		    dir = "unknown";
		    break;
		}
		len = pj_ansi_sprintf( info+info_len,
				       ", stream #%d: %.*s (%s)", mi,
				       (int)si->fmt.encoding_name.slen,
				       si->fmt.encoding_name.ptr,
				       dir);
		if (len > 0)
		    info_len += len;
		PJ_LOG(4,(THIS_FILE,"Audio updated%s", info));
	    }


	    if (call->audio_idx==-1 && status==PJ_SUCCESS &&
		si->dir != PJMEDIA_DIR_NONE)
	    {
		call->audio_idx = mi;
	    }

#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)
	} else if (call_med->type==PJMEDIA_TYPE_VIDEO) {
	    pjmedia_vid_stream_info the_si, *si = &the_si;

	    status = pjmedia_vid_stream_info_from_sdp(si, tmp_pool, pjsua_var.med_endpt,
						      local_sdp, remote_sdp, mi);
	    if (status != PJ_SUCCESS) {
		PJ_PERROR(1,(THIS_FILE, status,
			     "pjmedia_vid_stream_info_from_sdp() failed "
			         "for call_id %d media %d",
			     call_id, mi));
		continue;
	    }

	    /* Check if no media is active */
	    if (si->dir == PJMEDIA_DIR_NONE) {
		/* Update call media state and direction */
		call_med->state = PJSUA_CALL_MEDIA_NONE;
		call_med->dir = PJMEDIA_DIR_NONE;

	    } else {
		pjmedia_transport_info tp_info;

		/* Start/restart media transport */
		status = pjmedia_transport_media_start(call_med->tp,
						       tmp_pool, local_sdp,
						       remote_sdp, mi);
		if (status != PJ_SUCCESS) {
		    PJ_PERROR(1,(THIS_FILE, status,
				 "pjmedia_transport_media_start() failed "
				     "for call_id %d media %d",
				 call_id, mi));
		    continue;
		}

		pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_RUNNING);

		/* Get remote SRTP usage policy */
		pjmedia_transport_info_init(&tp_info);
		pjmedia_transport_get_info(call_med->tp, &tp_info);
		if (tp_info.specific_info_cnt > 0) {
		    unsigned i;
		    for (i = 0; i < tp_info.specific_info_cnt; ++i) {
			if (tp_info.spc_info[i].type ==
				PJMEDIA_TRANSPORT_TYPE_SRTP)
			{
			    pjmedia_srtp_info *sri;
			    sri=(pjmedia_srtp_info*)tp_info.spc_info[i].buffer;
			    call_med->rem_srtp_use = sri->peer_use;
			    break;
			}
		    }
		}

		/* Call media direction */
		call_med->dir = si->dir;

		/* Call media state */
		if (call->local_hold)
		    call_med->state = PJSUA_CALL_MEDIA_LOCAL_HOLD;
		else if (call_med->dir == PJMEDIA_DIR_DECODING)
		    call_med->state = PJSUA_CALL_MEDIA_REMOTE_HOLD;
		else
		    call_med->state = PJSUA_CALL_MEDIA_ACTIVE;
	    }

	    status = pjsua_vid_channel_update(call_med, tmp_pool, si,
	                                      local_sdp, remote_sdp);
	    if (status != PJ_SUCCESS) {
		PJ_PERROR(1,(THIS_FILE, status,
			     "pjsua_vid_channel_update() failed "
				 "for call_id %d media %d",
			     call_id, mi));
		continue;
	    }

	    /* Print info. */
	    {
		char info[80];
		int info_len = 0;
		int len;
		const char *dir;

		switch (si->dir) {
		case PJMEDIA_DIR_NONE:
		    dir = "inactive";
		    break;
		case PJMEDIA_DIR_ENCODING:
		    dir = "sendonly";
		    break;
		case PJMEDIA_DIR_DECODING:
		    dir = "recvonly";
		    break;
		case PJMEDIA_DIR_ENCODING_DECODING:
		    dir = "sendrecv";
		    break;
		default:
		    dir = "unknown";
		    break;
		}
		len = pj_ansi_sprintf( info+info_len,
				       ", stream #%d: %.*s (%s)", mi,
				       (int)si->codec_info.encoding_name.slen,
				       si->codec_info.encoding_name.ptr,
				       dir);
		if (len > 0)
		    info_len += len;
		PJ_LOG(4,(THIS_FILE,"Video updated%s", info));
	    }

#endif
	} else {
	    status = PJMEDIA_EINVALIMEDIATYPE;
	}

	/* Close the transport of deactivated media, need this here as media
	 * can be deactivated by the SDP negotiation and the max media count
	 * (account) setting.
	 */
	if (local_sdp->media[mi]->desc.port==0 && call_med->tp) {
	    pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_NULL);
	    pjmedia_transport_close(call_med->tp);
	    call_med->tp = call_med->tp_orig = NULL;
	}

	if (status != PJ_SUCCESS) {
	    PJ_PERROR(1,(THIS_FILE, status, "Error updating media call%02d:%d",
		         call_id, mi));
	} else {
	    got_media = PJ_TRUE;
	}
    }

    /* Update call media from provisional media */
    call->med_cnt = call->med_prov_cnt;
    pj_memcpy(call->media, call->media_prov,
	      sizeof(call->media_prov[0]) * call->med_prov_cnt);

    /* Perform SDP re-negotiation if needed. */
    if (got_media && need_renego_sdp) {
	pjmedia_sdp_neg *neg = call->inv->neg;

	/* This should only happen when we are the answerer. */
	PJ_ASSERT_RETURN(neg && !pjmedia_sdp_neg_was_answer_remote(neg),
			 PJMEDIA_SDPNEG_EINSTATE);
	
	status = pjmedia_sdp_neg_set_remote_offer(tmp_pool, neg, remote_sdp);
	if (status != PJ_SUCCESS)
	    goto on_error;

	status = pjmedia_sdp_neg_set_local_answer(tmp_pool, neg, local_sdp);
	if (status != PJ_SUCCESS)
	    goto on_error;

	status = pjmedia_sdp_neg_negotiate(tmp_pool, neg, 0);
	if (status != PJ_SUCCESS)
	    goto on_error;
    }

    pj_log_pop_indent();
    return (got_media? PJ_SUCCESS : PJMEDIA_SDPNEG_ENOMEDIA);

on_error:
    pj_log_pop_indent();
    return status;
}

/*****************************************************************************
 * Codecs.
 */

/*
 * Enum all supported codecs in the system.
 */
PJ_DEF(pj_status_t) pjsua_enum_codecs( pjsua_codec_info id[],
				       unsigned *p_count )
{
    pjmedia_codec_mgr *codec_mgr;
    pjmedia_codec_info info[32];
    unsigned i, count, prio[32];
    pj_status_t status;

    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);
    count = PJ_ARRAY_SIZE(info);
    status = pjmedia_codec_mgr_enum_codecs( codec_mgr, &count, info, prio);
    if (status != PJ_SUCCESS) {
	*p_count = 0;
	return status;
    }

    if (count > *p_count) count = *p_count;

    for (i=0; i<count; ++i) {
	pj_bzero(&id[i], sizeof(pjsua_codec_info));

	pjmedia_codec_info_to_id(&info[i], id[i].buf_, sizeof(id[i].buf_));
	id[i].codec_id = pj_str(id[i].buf_);
	id[i].priority = (pj_uint8_t) prio[i];
    }

    *p_count = count;

    return PJ_SUCCESS;
}


/*
 * Change codec priority.
 */
PJ_DEF(pj_status_t) pjsua_codec_set_priority( const pj_str_t *codec_id,
					      pj_uint8_t priority )
{
    const pj_str_t all = { NULL, 0 };
    pjmedia_codec_mgr *codec_mgr;

    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);

    if (codec_id->slen==1 && *codec_id->ptr=='*')
	codec_id = &all;

    return pjmedia_codec_mgr_set_codec_priority(codec_mgr, codec_id, 
					        priority);
}


/*
 * Get codec parameters.
 */
PJ_DEF(pj_status_t) pjsua_codec_get_param( const pj_str_t *codec_id,
					   pjmedia_codec_param *param )
{
    const pj_str_t all = { NULL, 0 };
    const pjmedia_codec_info *info;
    pjmedia_codec_mgr *codec_mgr;
    unsigned count = 1;
    pj_status_t status;

    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);

    if (codec_id->slen==1 && *codec_id->ptr=='*')
	codec_id = &all;

    status = pjmedia_codec_mgr_find_codecs_by_id(codec_mgr, codec_id,
						 &count, &info, NULL);
    if (status != PJ_SUCCESS)
	return status;

    if (count != 1)
	return (count > 1? PJ_ETOOMANY : PJ_ENOTFOUND);

    status = pjmedia_codec_mgr_get_default_param( codec_mgr, info, param);
    return status;
}


/*
 * Set codec parameters.
 */
PJ_DEF(pj_status_t) pjsua_codec_set_param( const pj_str_t *codec_id,
					   const pjmedia_codec_param *param)
{
    const pjmedia_codec_info *info[2];
    pjmedia_codec_mgr *codec_mgr;
    unsigned count = 2;
    pj_status_t status;

    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);

    status = pjmedia_codec_mgr_find_codecs_by_id(codec_mgr, codec_id,
						 &count, info, NULL);
    if (status != PJ_SUCCESS)
	return status;

    /* Codec ID should be specific, except for G.722.1 */
    if (count > 1 && 
	pj_strnicmp2(codec_id, "G7221/16", 8) != 0 &&
	pj_strnicmp2(codec_id, "G7221/32", 8) != 0)
    {
	pj_assert(!"Codec ID is not specific");
	return PJ_ETOOMANY;
    }

    status = pjmedia_codec_mgr_set_default_param(codec_mgr, info[0], param);
    return status;
}


pj_status_t pjsua_media_apply_xml_control(pjsua_call_id call_id,
					  const pj_str_t *xml_st)
{
#if PJMEDIA_HAS_VIDEO
    pjsua_call *call = &pjsua_var.calls[call_id];
    const pj_str_t PICT_FAST_UPDATE = {"picture_fast_update", 19};

    if (pj_strstr(xml_st, &PICT_FAST_UPDATE)) {
	unsigned i;

	PJ_LOG(4,(THIS_FILE, "Received keyframe request via SIP INFO"));

	for (i = 0; i < call->med_cnt; ++i) {
	    pjsua_call_media *cm = &call->media[i];
	    if (cm->type != PJMEDIA_TYPE_VIDEO || !cm->strm.v.stream)
		continue;

	    pjmedia_vid_stream_send_keyframe(cm->strm.v.stream);
	}

	return PJ_SUCCESS;
    }
#endif

    /* Just to avoid compiler warning of unused var */
    PJ_UNUSED_ARG(call_id);
    PJ_UNUSED_ARG(xml_st);

    return PJ_ENOTSUP;
}

