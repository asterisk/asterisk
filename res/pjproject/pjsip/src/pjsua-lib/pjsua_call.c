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


#define THIS_FILE		"pjsua_call.c"


/* Retry interval of sending re-INVITE for locking a codec when remote
 * SDP answer contains multiple codec, in milliseconds.
 */
#define LOCK_CODEC_RETRY_INTERVAL   200

/*
 * Max UPDATE/re-INVITE retry to lock codec
 */
#define LOCK_CODEC_MAX_RETRY	     5


/*
 * The INFO method.
 */
const pjsip_method pjsip_info_method = 
{
    PJSIP_OTHER_METHOD,
    { "INFO", 4 }
};


/* This callback receives notification from invite session when the
 * session state has changed.
 */
static void pjsua_call_on_state_changed(pjsip_inv_session *inv, 
					pjsip_event *e);

/* This callback is called by invite session framework when UAC session
 * has forked.
 */
static void pjsua_call_on_forked( pjsip_inv_session *inv, 
				  pjsip_event *e);

/*
 * Callback to be called when SDP offer/answer negotiation has just completed
 * in the session. This function will start/update media if negotiation
 * has succeeded.
 */
static void pjsua_call_on_media_update(pjsip_inv_session *inv,
				       pj_status_t status);

/*
 * Called when session received new offer.
 */
static void pjsua_call_on_rx_offer(pjsip_inv_session *inv,
				   const pjmedia_sdp_session *offer);

/*
 * Called to generate new offer.
 */
static void pjsua_call_on_create_offer(pjsip_inv_session *inv,
				       pjmedia_sdp_session **offer);

/*
 * This callback is called when transaction state has changed in INVITE
 * session. We use this to trap:
 *  - incoming REFER request.
 *  - incoming MESSAGE request.
 */
static void pjsua_call_on_tsx_state_changed(pjsip_inv_session *inv,
					    pjsip_transaction *tsx,
					    pjsip_event *e);

/*
 * Redirection handler.
 */
static pjsip_redirect_op pjsua_call_on_redirected(pjsip_inv_session *inv,
						  const pjsip_uri *target,
						  const pjsip_event *e);


/* Create SDP for call hold. */
static pj_status_t create_sdp_of_call_hold(pjsua_call *call,
					   pjmedia_sdp_session **p_sdp);

/*
 * Callback called by event framework when the xfer subscription state
 * has changed.
 */
static void xfer_client_on_evsub_state( pjsip_evsub *sub, pjsip_event *event);
static void xfer_server_on_evsub_state( pjsip_evsub *sub, pjsip_event *event);

/*
 * Reset call descriptor.
 */
static void reset_call(pjsua_call_id id)
{
    pjsua_call *call = &pjsua_var.calls[id];
    unsigned i;

    pj_bzero(call, sizeof(*call));
    call->index = id;
    call->last_text.ptr = call->last_text_buf_;
    for (i=0; i<PJ_ARRAY_SIZE(call->media); ++i) {
	pjsua_call_media *call_med = &call->media[i];
	call_med->ssrc = pj_rand();
	call_med->strm.a.conf_slot = PJSUA_INVALID_ID;
	call_med->strm.v.cap_win_id = PJSUA_INVALID_ID;
	call_med->strm.v.rdr_win_id = PJSUA_INVALID_ID;
	call_med->call = call;
	call_med->idx = i;
	call_med->tp_auto_del = PJ_TRUE;
    }
    pjsua_call_setting_default(&call->opt);
}


/*
 * Init call subsystem.
 */
pj_status_t pjsua_call_subsys_init(const pjsua_config *cfg)
{
    pjsip_inv_callback inv_cb;
    unsigned i;
    const pj_str_t str_norefersub = { "norefersub", 10 };
    pj_status_t status;

    /* Init calls array. */
    for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.calls); ++i)
	reset_call(i);

    /* Copy config */
    pjsua_config_dup(pjsua_var.pool, &pjsua_var.ua_cfg, cfg);

    /* Verify settings */
    if (pjsua_var.ua_cfg.max_calls >= PJSUA_MAX_CALLS) {
	pjsua_var.ua_cfg.max_calls = PJSUA_MAX_CALLS;
    }

    /* Check the route URI's and force loose route if required */
    for (i=0; i<pjsua_var.ua_cfg.outbound_proxy_cnt; ++i) {
	status = normalize_route_uri(pjsua_var.pool, 
				     &pjsua_var.ua_cfg.outbound_proxy[i]);
	if (status != PJ_SUCCESS)
	    return status;
    }

    /* Initialize invite session callback. */
    pj_bzero(&inv_cb, sizeof(inv_cb));
    inv_cb.on_state_changed = &pjsua_call_on_state_changed;
    inv_cb.on_new_session = &pjsua_call_on_forked;
    inv_cb.on_media_update = &pjsua_call_on_media_update;
    inv_cb.on_rx_offer = &pjsua_call_on_rx_offer;
    inv_cb.on_create_offer = &pjsua_call_on_create_offer;
    inv_cb.on_tsx_state_changed = &pjsua_call_on_tsx_state_changed;
    inv_cb.on_redirected = &pjsua_call_on_redirected;

    /* Initialize invite session module: */
    status = pjsip_inv_usage_init(pjsua_var.endpt, &inv_cb);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

    /* Add "norefersub" in Supported header */
    pjsip_endpt_add_capability(pjsua_var.endpt, NULL, PJSIP_H_SUPPORTED,
			       NULL, 1, &str_norefersub);

    return status;
}


/*
 * Start call subsystem.
 */
pj_status_t pjsua_call_subsys_start(void)
{
    /* Nothing to do */
    return PJ_SUCCESS;
}


/*
 * Get maximum number of calls configured in pjsua.
 */
PJ_DEF(unsigned) pjsua_call_get_max_count(void)
{
    return pjsua_var.ua_cfg.max_calls;
}


/*
 * Get number of currently active calls.
 */
PJ_DEF(unsigned) pjsua_call_get_count(void)
{
    return pjsua_var.call_cnt;
}


/*
 * Enum calls.
 */
PJ_DEF(pj_status_t) pjsua_enum_calls( pjsua_call_id ids[],
				      unsigned *count)
{
    unsigned i, c;

    PJ_ASSERT_RETURN(ids && *count, PJ_EINVAL);

    PJSUA_LOCK();

    for (i=0, c=0; c<*count && i<pjsua_var.ua_cfg.max_calls; ++i) {
	if (!pjsua_var.calls[i].inv)
	    continue;
	ids[c] = i;
	++c;
    }

    *count = c;

    PJSUA_UNLOCK();

    return PJ_SUCCESS;
}


/* Allocate one call id */
static pjsua_call_id alloc_call_id(void)
{
    pjsua_call_id cid;

#if 1
    /* New algorithm: round-robin */
    if (pjsua_var.next_call_id >= (int)pjsua_var.ua_cfg.max_calls || 
	pjsua_var.next_call_id < 0)
    {
	pjsua_var.next_call_id = 0;
    }

    for (cid=pjsua_var.next_call_id; 
	 cid<(int)pjsua_var.ua_cfg.max_calls; 
	 ++cid) 
    {
	if (pjsua_var.calls[cid].inv == NULL &&
            pjsua_var.calls[cid].async_call.dlg == NULL)
        {
	    ++pjsua_var.next_call_id;
	    return cid;
	}
    }

    for (cid=0; cid < pjsua_var.next_call_id; ++cid) {
	if (pjsua_var.calls[cid].inv == NULL &&
            pjsua_var.calls[cid].async_call.dlg == NULL)
        {
	    ++pjsua_var.next_call_id;
	    return cid;
	}
    }

#else
    /* Old algorithm */
    for (cid=0; cid<(int)pjsua_var.ua_cfg.max_calls; ++cid) {
	if (pjsua_var.calls[cid].inv == NULL)
	    return cid;
    }
#endif

    return PJSUA_INVALID_ID;
}

/* Get signaling secure level.
 * Return:
 *  0: if signaling is not secure
 *  1: if TLS transport is used for immediate hop
 *  2: if end-to-end signaling is secure.
 */
static int get_secure_level(pjsua_acc_id acc_id, const pj_str_t *dst_uri)
{
    const pj_str_t tls = pj_str(";transport=tls");
    const pj_str_t sips = pj_str("sips:");
    pjsua_acc *acc = &pjsua_var.acc[acc_id];

    if (pj_stristr(dst_uri, &sips))
	return 2;
    
    if (!pj_list_empty(&acc->route_set)) {
	pjsip_route_hdr *r = acc->route_set.next;
	pjsip_uri *uri = r->name_addr.uri;
	pjsip_sip_uri *sip_uri;
	
	sip_uri = (pjsip_sip_uri*)pjsip_uri_get_uri(uri);
	if (pj_stricmp2(&sip_uri->transport_param, "tls")==0)
	    return 1;

    } else {
	if (pj_stristr(dst_uri, &tls))
	    return 1;
    }

    return 0;
}

/*
static int call_get_secure_level(pjsua_call *call)
{
    if (call->inv->dlg->secure)
	return 2;

    if (!pj_list_empty(&call->inv->dlg->route_set)) {
	pjsip_route_hdr *r = call->inv->dlg->route_set.next;
	pjsip_uri *uri = r->name_addr.uri;
	pjsip_sip_uri *sip_uri;
	
	sip_uri = (pjsip_sip_uri*)pjsip_uri_get_uri(uri);
	if (pj_stricmp2(&sip_uri->transport_param, "tls")==0)
	    return 1;

    } else {
	pjsip_sip_uri *sip_uri;

	if (PJSIP_URI_SCHEME_IS_SIPS(call->inv->dlg->target))
	    return 2;
	if (!PJSIP_URI_SCHEME_IS_SIP(call->inv->dlg->target))
	    return 0;

	sip_uri = (pjsip_sip_uri*) pjsip_uri_get_uri(call->inv->dlg->target);
	if (pj_stricmp2(&sip_uri->transport_param, "tls")==0)
	    return 1;
    }

    return 0;
}
*/

/* Outgoing call callback when media transport creation is completed. */
static pj_status_t
on_make_call_med_tp_complete(pjsua_call_id call_id,
                             const pjsua_med_tp_state_info *info)
{
    pjmedia_sdp_session *offer;
    pjsip_inv_session *inv = NULL;
    pjsua_call *call = &pjsua_var.calls[call_id];
    pjsua_acc *acc = &pjsua_var.acc[call->acc_id];
    pjsip_dialog *dlg = call->async_call.dlg;
    unsigned options = 0;
    pjsip_tx_data *tdata;
    pj_status_t status = (info? info->status: PJ_SUCCESS);

    PJSUA_LOCK();

    /* Increment the dialog's lock otherwise when invite session creation
     * fails the dialog will be destroyed prematurely.
     */
    pjsip_dlg_inc_lock(dlg);

    /* Decrement dialog session. */
    pjsip_dlg_dec_session(dlg, &pjsua_var.mod);

    if (status != PJ_SUCCESS) {
	pj_str_t err_str;
	int title_len;

	call->last_code = PJSIP_SC_TEMPORARILY_UNAVAILABLE;
	pj_strcpy2(&call->last_text, "Media init error: ");

	title_len = call->last_text.slen;
	err_str = pj_strerror(status, call->last_text_buf_ + title_len,
	                      sizeof(call->last_text_buf_) - title_len);
	call->last_text.slen += err_str.slen;

	pjsua_perror(THIS_FILE, "Error initializing media channel", status);
	goto on_error;
    }

    /* pjsua_media_channel_deinit() has been called. */
    if (call->async_call.med_ch_deinit)
        goto on_error;

    /* Create offer */
    status = pjsua_media_channel_create_sdp(call->index, dlg->pool, NULL,
					    &offer, NULL);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error initializing media channel", status);
	goto on_error;
    }

    /* Create the INVITE session: */
    options |= PJSIP_INV_SUPPORT_100REL;
    if (acc->cfg.require_100rel)
	options |= PJSIP_INV_REQUIRE_100REL;
    if (acc->cfg.use_timer != PJSUA_SIP_TIMER_INACTIVE) {
	options |= PJSIP_INV_SUPPORT_TIMER;
	if (acc->cfg.use_timer == PJSUA_SIP_TIMER_REQUIRED)
	    options |= PJSIP_INV_REQUIRE_TIMER;
	else if (acc->cfg.use_timer == PJSUA_SIP_TIMER_ALWAYS)
	    options |= PJSIP_INV_ALWAYS_USE_TIMER;
    }

    status = pjsip_inv_create_uac( dlg, offer, options, &inv);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Invite session creation failed", status);
	goto on_error;
    }

    /* Init Session Timers */
    status = pjsip_timer_init_session(inv, &acc->cfg.timer_setting);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Session Timer init failed", status);
	goto on_error;
    }

    /* Create and associate our data in the session. */
    call->inv = inv;

    dlg->mod_data[pjsua_var.mod.id] = call;
    inv->mod_data[pjsua_var.mod.id] = call;

    /* If account is locked to specific transport, then lock dialog
     * to this transport too.
     */
    if (acc->cfg.transport_id != PJSUA_INVALID_ID) {
	pjsip_tpselector tp_sel;

	pjsua_init_tpselector(acc->cfg.transport_id, &tp_sel);
	pjsip_dlg_set_transport(dlg, &tp_sel);
    }

    /* Set dialog Route-Set: */
    if (!pj_list_empty(&acc->route_set))
	pjsip_dlg_set_route_set(dlg, &acc->route_set);


    /* Set credentials: */
    if (acc->cred_cnt) {
	pjsip_auth_clt_set_credentials( &dlg->auth_sess, 
					acc->cred_cnt, acc->cred);
    }

    /* Set authentication preference */
    pjsip_auth_clt_set_prefs(&dlg->auth_sess, &acc->cfg.auth_pref);

    /* Create initial INVITE: */

    status = pjsip_inv_invite(inv, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create initial INVITE request", 
		     status);
	goto on_error;
    }


    /* Add additional headers etc */

    pjsua_process_msg_data( tdata,
                            call->async_call.call_var.out_call.msg_data);

    /* Must increment call counter now */
    ++pjsua_var.call_cnt;

    /* Send initial INVITE: */

    status = pjsip_inv_send_msg(inv, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send initial INVITE request", 
		     status);

	/* Upon failure to send first request, the invite 
	 * session would have been cleared.
	 */
	inv = NULL;
	goto on_error;
    }

    /* Done. */

    pjsip_dlg_dec_lock(dlg);
    PJSUA_UNLOCK();

    return PJ_SUCCESS;

on_error:
    if (inv == NULL && call_id != -1 && pjsua_var.ua_cfg.cb.on_call_state)
        (*pjsua_var.ua_cfg.cb.on_call_state)(call_id, NULL);

    if (dlg) {
	/* This may destroy the dialog */
	pjsip_dlg_dec_lock(dlg);
    }

    if (inv != NULL) {
	pjsip_inv_terminate(inv, PJSIP_SC_OK, PJ_FALSE);
    }

    if (call_id != -1) {
	reset_call(call_id);
	pjsua_media_channel_deinit(call_id);
    }

    PJSUA_UNLOCK();
    return status;
}


/*
 * Initialize call settings based on account ID.
 */
PJ_DEF(void) pjsua_call_setting_default(pjsua_call_setting *opt)
{
    pj_assert(opt);

    pj_bzero(opt, sizeof(*opt));
    opt->flag = PJSUA_CALL_INCLUDE_DISABLED_MEDIA;
    opt->aud_cnt = 1;

#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)
    opt->vid_cnt = 1;
    opt->req_keyframe_method = PJSUA_VID_REQ_KEYFRAME_SIP_INFO |
			     PJSUA_VID_REQ_KEYFRAME_RTCP_PLI;
#endif
}

static pj_status_t apply_call_setting(pjsua_call *call,
				      const pjsua_call_setting *opt,
				      const pjmedia_sdp_session *rem_sdp)
{
    pj_assert(call);

    if (!opt)
	return PJ_SUCCESS;

#if !PJMEDIA_HAS_VIDEO
    pj_assert(opt->vid_cnt == 0);
#endif

    /* If call is established, reinit media channel */
    if (call->inv && call->inv->state == PJSIP_INV_STATE_CONFIRMED) {
	pjsua_call_setting old_opt;
	pj_status_t status;

	old_opt = call->opt;
	call->opt = *opt;

	/* Reinit media channel when media count is changed or we are the
	 * answerer (as remote offer may 'extremely' modify the existing
	 * media session, e.g: media type order).
	 */
	if (rem_sdp ||
	    opt->aud_cnt!=old_opt.aud_cnt || opt->vid_cnt!=old_opt.vid_cnt)
	{
	    pjsip_role_e role = rem_sdp? PJSIP_ROLE_UAS : PJSIP_ROLE_UAC;
	    status = pjsua_media_channel_init(call->index, role,
					      call->secure_level,
					      call->inv->pool_prov,
					      rem_sdp, NULL,
					      PJ_FALSE, NULL);
	    if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "Error re-initializing media channel",
			     status);
		return status;
	    }
	}
    } else {
	call->opt = *opt;
    }

    return PJ_SUCCESS;
}

/*
 * Make outgoing call to the specified URI using the specified account.
 */
PJ_DEF(pj_status_t) pjsua_call_make_call(pjsua_acc_id acc_id,
					 const pj_str_t *dest_uri,
					 const pjsua_call_setting *opt,
					 void *user_data,
					 const pjsua_msg_data *msg_data,
					 pjsua_call_id *p_call_id)
{
    pj_pool_t *tmp_pool = NULL;
    pjsip_dialog *dlg = NULL;
    pjsua_acc *acc;
    pjsua_call *call;
    int call_id = -1;
    pj_str_t contact;
    pj_status_t status;


    /* Check that account is valid */
    PJ_ASSERT_RETURN(acc_id>=0 || acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc), 
		     PJ_EINVAL);

    /* Check arguments */
    PJ_ASSERT_RETURN(dest_uri, PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Making call with acc #%d to %.*s", acc_id,
	      (int)dest_uri->slen, dest_uri->ptr));

    pj_log_push_indent();

    PJSUA_LOCK();

    /* Create sound port if none is instantiated, to check if sound device
     * can be used. But only do this with the conference bridge, as with 
     * audio switchboard (i.e. APS-Direct), we can only open the sound 
     * device once the correct format has been known
     */
    if (!pjsua_var.is_mswitch && pjsua_var.snd_port==NULL && 
	pjsua_var.null_snd==NULL && !pjsua_var.no_snd) 
    {
	status = pjsua_set_snd_dev(pjsua_var.cap_dev, pjsua_var.play_dev);
	if (status != PJ_SUCCESS)
	    goto on_error;
    }

    acc = &pjsua_var.acc[acc_id];
    if (!acc->valid) {
	pjsua_perror(THIS_FILE, "Unable to make call because account "
		     "is not valid", PJ_EINVALIDOP);
	status = PJ_EINVALIDOP;
	goto on_error;
    }

    /* Find free call slot. */
    call_id = alloc_call_id();

    if (call_id == PJSUA_INVALID_ID) {
	pjsua_perror(THIS_FILE, "Error making call", PJ_ETOOMANY);
	status = PJ_ETOOMANY;
	goto on_error;
    }

    call = &pjsua_var.calls[call_id];

    /* Associate session with account */
    call->acc_id = acc_id;
    call->call_hold_type = acc->cfg.call_hold_type;

    /* Apply call setting */
    status = apply_call_setting(call, opt, NULL);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Failed to apply call setting", status);
	goto on_error;
    }

    /* Create temporary pool */
    tmp_pool = pjsua_pool_create("tmpcall10", 512, 256);

    /* Verify that destination URI is valid before calling 
     * pjsua_acc_create_uac_contact, or otherwise there  
     * a misleading "Invalid Contact URI" error will be printed
     * when pjsua_acc_create_uac_contact() fails.
     */
    if (1) {
	pjsip_uri *uri;
	pj_str_t dup;

	pj_strdup_with_null(tmp_pool, &dup, dest_uri);
	uri = pjsip_parse_uri(tmp_pool, dup.ptr, dup.slen, 0);

	if (uri == NULL) {
	    pjsua_perror(THIS_FILE, "Unable to make call", 
			 PJSIP_EINVALIDREQURI);
	    status = PJSIP_EINVALIDREQURI;
	    goto on_error;
	}
    }

    /* Mark call start time. */
    pj_gettimeofday(&call->start_time);

    /* Reset first response time */
    call->res_time.sec = 0;

    /* Create suitable Contact header unless a Contact header has been
     * set in the account.
     */
    if (acc->contact.slen) {
	contact = acc->contact;
    } else {
	status = pjsua_acc_create_uac_contact(tmp_pool, &contact,
					      acc_id, dest_uri);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to generate Contact header", 
			 status);
	    goto on_error;
	}
    }

    /* Create outgoing dialog: */
    status = pjsip_dlg_create_uac( pjsip_ua_instance(), 
				   &acc->cfg.id, &contact,
				   dest_uri, dest_uri, &dlg);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Dialog creation failed", status);
	goto on_error;
    }

    /* Increment the dialog's lock otherwise when invite session creation
     * fails the dialog will be destroyed prematurely.
     */
    pjsip_dlg_inc_lock(dlg);

    /* Calculate call's secure level */
    call->secure_level = get_secure_level(acc_id, dest_uri);

    /* Attach user data */
    call->user_data = user_data;
    
    /* Store variables required for the callback after the async
     * media transport creation is completed.
     */
    if (msg_data) {
	call->async_call.call_var.out_call.msg_data = pjsua_msg_data_clone(
                                                          dlg->pool, msg_data);
    }
    call->async_call.dlg = dlg;

    /* Temporarily increment dialog session. Without this, dialog will be
     * prematurely destroyed if dec_lock() is called on the dialog before
     * the invite session is created.
     */
    pjsip_dlg_inc_session(dlg, &pjsua_var.mod);

    /* Init media channel */
    status = pjsua_media_channel_init(call->index, PJSIP_ROLE_UAC, 
				      call->secure_level, dlg->pool,
				      NULL, NULL, PJ_TRUE,
                                      &on_make_call_med_tp_complete);
    if (status == PJ_SUCCESS) {
        status = on_make_call_med_tp_complete(call->index, NULL);
        if (status != PJ_SUCCESS)
	    goto on_error;
    } else if (status != PJ_EPENDING) {
	pjsua_perror(THIS_FILE, "Error initializing media channel", status);
        pjsip_dlg_dec_session(dlg, &pjsua_var.mod);
	goto on_error;
    }

    /* Done. */

    if (p_call_id)
	*p_call_id = call_id;

    pjsip_dlg_dec_lock(dlg);
    pj_pool_release(tmp_pool);
    PJSUA_UNLOCK();

    pj_log_pop_indent();

    return PJ_SUCCESS;


on_error:
    if (dlg) {
	/* This may destroy the dialog */
	pjsip_dlg_dec_lock(dlg);
    }

    if (call_id != -1) {
	reset_call(call_id);
	pjsua_media_channel_deinit(call_id);
    }

    if (tmp_pool)
	pj_pool_release(tmp_pool);
    PJSUA_UNLOCK();

    pj_log_pop_indent();
    return status;
}


/* Get the NAT type information in remote's SDP */
static void update_remote_nat_type(pjsua_call *call, 
				   const pjmedia_sdp_session *sdp)
{
    const pjmedia_sdp_attr *xnat;

    xnat = pjmedia_sdp_attr_find2(sdp->attr_count, sdp->attr, "X-nat", NULL);
    if (xnat) {
	call->rem_nat_type = (pj_stun_nat_type) (xnat->value.ptr[0] - '0');
    } else {
	call->rem_nat_type = PJ_STUN_NAT_TYPE_UNKNOWN;
    }

    PJ_LOG(5,(THIS_FILE, "Call %d: remote NAT type is %d (%s)", call->index,
	      call->rem_nat_type, pj_stun_get_nat_name(call->rem_nat_type)));
}


/* Incoming call callback when media transport creation is completed. */
static pj_status_t
on_incoming_call_med_tp_complete(pjsua_call_id call_id,
                                 const pjsua_med_tp_state_info *info)
{
    pjsua_call *call = &pjsua_var.calls[call_id];
    const pjmedia_sdp_session *offer=NULL;
    pjmedia_sdp_session *answer;
    pjsip_tx_data *response = NULL;
    unsigned options = 0;
    int sip_err_code = (info? info->sip_err_code: 0);
    pj_status_t status = (info? info->status: PJ_SUCCESS);

    PJSUA_LOCK();

    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error initializing media channel", status);
        goto on_return;
    }

    /* pjsua_media_channel_deinit() has been called. */
    if (call->async_call.med_ch_deinit) {
        pjsua_media_channel_deinit(call->index);
        call->med_ch_cb = NULL;
        PJSUA_UNLOCK();
        return PJ_SUCCESS;
    }

    /* Get remote SDP offer (if any). */
    if (call->inv->neg)
        pjmedia_sdp_neg_get_neg_remote(call->inv->neg, &offer);

    status = pjsua_media_channel_create_sdp(call_id,
                                            call->async_call.dlg->pool, 
					    offer, &answer, &sip_err_code);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error creating SDP answer", status);
        goto on_return;
    }

    status = pjsip_inv_set_local_sdp(call->inv, answer);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error setting local SDP", status);
        sip_err_code = PJSIP_SC_NOT_ACCEPTABLE_HERE;
        goto on_return;
    }

    /* Verify that we can handle the request. */
    status = pjsip_inv_verify_request3(NULL,
                                       call->inv->pool_prov, &options, offer,
                                       answer, NULL, pjsua_var.endpt, &response);
    if (status != PJ_SUCCESS) {
	/*
	 * No we can't handle the incoming INVITE request.
	 */
        sip_err_code = PJSIP_ERRNO_TO_SIP_STATUS(status);
	goto on_return;
    } 

on_return:
    if (status != PJ_SUCCESS) {
        /* If the callback is called from pjsua_call_on_incoming(), the
         * invite's state is PJSIP_INV_STATE_NULL, so the invite session
         * will be terminated later, otherwise we end the session here.
         */
        if (call->inv->state > PJSIP_INV_STATE_NULL) {
            pjsip_tx_data *tdata;
            pj_status_t status_;

	    status_ = pjsip_inv_end_session(call->inv, sip_err_code, NULL,
                                            &tdata);
	    if (status_ == PJ_SUCCESS && tdata)
	        status_ = pjsip_inv_send_msg(call->inv, tdata);
        }

        pjsua_media_channel_deinit(call->index);
    }

    /* Set the callback to NULL to indicate that the async operation
     * has completed.
     */
    call->med_ch_cb = NULL;

    if (status == PJ_SUCCESS &&
        !pj_list_empty(&call->async_call.call_var.inc_call.answers))
    {
        struct call_answer *answer, *next;
        
        answer = call->async_call.call_var.inc_call.answers.next;
        while (answer != &call->async_call.call_var.inc_call.answers) {
            next = answer->next;
            pjsua_call_answer(call_id, answer->code, answer->reason,
                              answer->msg_data);
            
            /* Call might have been disconnected if application is answering
             * with 200/OK and the media failed to start.
             * See pjsua_call_answer() below.
             */
            if (!call->inv || !call->inv->pool_prov)
                break;

            pj_list_erase(answer);
            answer = next;
        }
    }

    PJSUA_UNLOCK();
    return status;
}


/**
 * Handle incoming INVITE request.
 * Called by pjsua_core.c
 */
pj_bool_t pjsua_call_on_incoming(pjsip_rx_data *rdata)
{
    pj_str_t contact;
    pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);
    pjsip_dialog *replaced_dlg = NULL;
    pjsip_transaction *tsx = pjsip_rdata_get_tsx(rdata);
    pjsip_msg *msg = rdata->msg_info.msg;
    pjsip_tx_data *response = NULL;
    unsigned options = 0;
    pjsip_inv_session *inv = NULL;
    int acc_id;
    pjsua_call *call;
    int call_id = -1;
    int sip_err_code;
    pjmedia_sdp_session *offer=NULL;
    pj_status_t status;

    /* Don't want to handle anything but INVITE */
    if (msg->line.req.method.id != PJSIP_INVITE_METHOD)
	return PJ_FALSE;

    /* Don't want to handle anything that's already associated with
     * existing dialog or transaction.
     */
    if (dlg || tsx)
	return PJ_FALSE;

    /* Don't want to accept the call if shutdown is in progress */
    if (pjsua_var.thread_quit_flag) {
	pjsip_endpt_respond_stateless(pjsua_var.endpt, rdata, 
				      PJSIP_SC_TEMPORARILY_UNAVAILABLE, NULL,
				      NULL, NULL);
	return PJ_TRUE;
    }

    PJ_LOG(4,(THIS_FILE, "Incoming %s", rdata->msg_info.info));
    pj_log_push_indent();

    PJSUA_LOCK();

    /* Find free call slot. */
    call_id = alloc_call_id();

    if (call_id == PJSUA_INVALID_ID) {
	pjsip_endpt_respond_stateless(pjsua_var.endpt, rdata, 
				      PJSIP_SC_BUSY_HERE, NULL,
				      NULL, NULL);
	PJ_LOG(2,(THIS_FILE, 
		  "Unable to accept incoming call (too many calls)"));
	goto on_return;
    }

    /* Clear call descriptor */
    reset_call(call_id);

    call = &pjsua_var.calls[call_id];

    /* Mark call start time. */
    pj_gettimeofday(&call->start_time);

    /* Check INVITE request for Replaces header. If Replaces header is
     * present, the function will make sure that we can handle the request.
     */
    status = pjsip_replaces_verify_request(rdata, &replaced_dlg, PJ_FALSE,
					   &response);
    if (status != PJ_SUCCESS) {
	/*
	 * Something wrong with the Replaces header.
	 */
	if (response) {
	    pjsip_response_addr res_addr;

	    pjsip_get_response_addr(response->pool, rdata, &res_addr);
	    pjsip_endpt_send_response(pjsua_var.endpt, &res_addr, response, 
				      NULL, NULL);

	} else {

	    /* Respond with 500 (Internal Server Error) */
	    pjsip_endpt_respond_stateless(pjsua_var.endpt, rdata, 500, NULL,
					  NULL, NULL);
	}

	goto on_return;
    }

    /* If this INVITE request contains Replaces header, notify application
     * about the request so that application can do subsequent checking
     * if it wants to.
     */
    if (replaced_dlg != NULL &&
	(pjsua_var.ua_cfg.cb.on_call_replace_request ||
	 pjsua_var.ua_cfg.cb.on_call_replace_request2))
    {
	pjsua_call *replaced_call;
	int st_code = 200;
	pj_str_t st_text = { "OK", 2 };

	/* Get the replaced call instance */
	replaced_call = (pjsua_call*) replaced_dlg->mod_data[pjsua_var.mod.id];

	/* Copy call setting from the replaced call */
	call->opt = replaced_call->opt;

	/* Notify application */
	if (pjsua_var.ua_cfg.cb.on_call_replace_request) {
	    pjsua_var.ua_cfg.cb.on_call_replace_request(replaced_call->index,
							rdata,
							&st_code, &st_text);
	}

	if (pjsua_var.ua_cfg.cb.on_call_replace_request2) {
	    pjsua_var.ua_cfg.cb.on_call_replace_request2(replaced_call->index,
							 rdata,
							 &st_code, &st_text,
							 &call->opt);
	}

	/* Must specify final response */
	PJ_ASSERT_ON_FAIL(st_code >= 200, st_code = 200);

	/* Check if application rejects this request. */
	if (st_code >= 300) {

	    if (st_text.slen == 2)
		st_text = *pjsip_get_status_text(st_code);

	    pjsip_endpt_respond(pjsua_var.endpt, NULL, rdata, 
				st_code, &st_text, NULL, NULL, NULL);
	    goto on_return;
	}
    }

    /* 
     * Get which account is most likely to be associated with this incoming
     * call. We need the account to find which contact URI to put for
     * the call.
     */
    acc_id = call->acc_id = pjsua_acc_find_for_incoming(rdata);
    call->call_hold_type = pjsua_var.acc[acc_id].cfg.call_hold_type;

    /* Get call's secure level */
    if (PJSIP_URI_SCHEME_IS_SIPS(rdata->msg_info.msg->line.req.uri))
	call->secure_level = 2;
    else if (PJSIP_TRANSPORT_IS_SECURE(rdata->tp_info.transport))
	call->secure_level = 1;
    else
	call->secure_level = 0;

    /* Parse SDP from incoming request */
    if (rdata->msg_info.msg->body) {
	pjsip_rdata_sdp_info *sdp_info;

	sdp_info = pjsip_rdata_get_sdp_info(rdata);
	offer = sdp_info->sdp;

	status = sdp_info->sdp_err;
	if (status==PJ_SUCCESS && sdp_info->sdp==NULL)
	    status = PJSIP_ERRNO_FROM_SIP_STATUS(PJSIP_SC_NOT_ACCEPTABLE);

	if (status != PJ_SUCCESS) {
	    const pj_str_t reason = pj_str("Bad SDP");
	    pjsip_hdr hdr_list;
	    pjsip_warning_hdr *w;

	    pjsua_perror(THIS_FILE, "Bad SDP in incoming INVITE", 
			 status);

	    w = pjsip_warning_hdr_create_from_status(rdata->tp_info.pool, 
					     pjsip_endpt_name(pjsua_var.endpt),
					     status);
	    pj_list_init(&hdr_list);
	    pj_list_push_back(&hdr_list, w);

	    pjsip_endpt_respond(pjsua_var.endpt, NULL, rdata, 400, 
				&reason, &hdr_list, NULL, NULL);
	    goto on_return;
	}

	/* Do quick checks on SDP before passing it to transports. More elabore 
	 * checks will be done in pjsip_inv_verify_request2() below.
	 */
	if (offer->media_count==0) {
	    const pj_str_t reason = pj_str("Missing media in SDP");
	    pjsip_endpt_respond(pjsua_var.endpt, NULL, rdata, 400, &reason, 
				NULL, NULL, NULL);
	    goto on_return;
	}

    } else {
	offer = NULL;
    }

    /* Verify that we can handle the request. */
    options |= PJSIP_INV_SUPPORT_100REL;
    options |= PJSIP_INV_SUPPORT_TIMER;
    if (pjsua_var.acc[acc_id].cfg.require_100rel == PJSUA_100REL_MANDATORY)
	options |= PJSIP_INV_REQUIRE_100REL;
    if (pjsua_var.media_cfg.enable_ice)
	options |= PJSIP_INV_SUPPORT_ICE;
    if (pjsua_var.acc[acc_id].cfg.use_timer == PJSUA_SIP_TIMER_REQUIRED)
	options |= PJSIP_INV_REQUIRE_TIMER;
    else if (pjsua_var.acc[acc_id].cfg.use_timer == PJSUA_SIP_TIMER_ALWAYS)
	options |= PJSIP_INV_ALWAYS_USE_TIMER;

    status = pjsip_inv_verify_request2(rdata, &options, offer, NULL, NULL,
				       pjsua_var.endpt, &response);
    if (status != PJ_SUCCESS) {

	/*
	 * No we can't handle the incoming INVITE request.
	 */
	if (response) {
	    pjsip_response_addr res_addr;

	    pjsip_get_response_addr(response->pool, rdata, &res_addr);
	    pjsip_endpt_send_response(pjsua_var.endpt, &res_addr, response, 
				      NULL, NULL);

	} else {
	    /* Respond with 500 (Internal Server Error) */
	    pjsip_endpt_respond(pjsua_var.endpt, NULL, rdata, 500, NULL,
				NULL, NULL, NULL);
	}

	goto on_return;
    } 

    /* Get suitable Contact header */
    if (pjsua_var.acc[acc_id].contact.slen) {
	contact = pjsua_var.acc[acc_id].contact;
    } else {
	status = pjsua_acc_create_uas_contact(rdata->tp_info.pool, &contact,
					      acc_id, rdata);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to generate Contact header", 
			 status);
	    pjsip_endpt_respond_stateless(pjsua_var.endpt, rdata, 500, NULL,
					  NULL, NULL);
	    goto on_return;
	}
    }

    /* Create dialog: */
    status = pjsip_dlg_create_uas( pjsip_ua_instance(), rdata,
				   &contact, &dlg);
    if (status != PJ_SUCCESS) {
	pjsip_endpt_respond_stateless(pjsua_var.endpt, rdata, 500, NULL,
				      NULL, NULL);
	goto on_return;
    }

    /* Set credentials */
    if (pjsua_var.acc[acc_id].cred_cnt) {
	pjsip_auth_clt_set_credentials(&dlg->auth_sess, 
				       pjsua_var.acc[acc_id].cred_cnt,
				       pjsua_var.acc[acc_id].cred);
    }

    /* Set preference */
    pjsip_auth_clt_set_prefs(&dlg->auth_sess, 
			     &pjsua_var.acc[acc_id].cfg.auth_pref);

    /* Disable Session Timers if not prefered and the incoming INVITE request
     * did not require it.
     */
    if (pjsua_var.acc[acc_id].cfg.use_timer == PJSUA_SIP_TIMER_INACTIVE && 
	(options & PJSIP_INV_REQUIRE_TIMER) == 0)
    {
	options &= ~(PJSIP_INV_SUPPORT_TIMER);
    }

    /* If 100rel is optional and UAC supports it, use it. */
    if ((options & PJSIP_INV_REQUIRE_100REL)==0 &&
	pjsua_var.acc[acc_id].cfg.require_100rel == PJSUA_100REL_OPTIONAL)
    {
	const pj_str_t token = { "100rel", 6};
	pjsip_dialog_cap_status cap_status;

	cap_status = pjsip_dlg_remote_has_cap(dlg, PJSIP_H_SUPPORTED, NULL,
	                                      &token);
	if (cap_status == PJSIP_DIALOG_CAP_SUPPORTED)
	    options |= PJSIP_INV_REQUIRE_100REL;
    }

    /* Create invite session: */
    status = pjsip_inv_create_uas( dlg, rdata, NULL, options, &inv);
    if (status != PJ_SUCCESS) {
	pjsip_hdr hdr_list;
	pjsip_warning_hdr *w;

	w = pjsip_warning_hdr_create_from_status(dlg->pool, 
						 pjsip_endpt_name(pjsua_var.endpt),
						 status);
	pj_list_init(&hdr_list);
	pj_list_push_back(&hdr_list, w);

	pjsip_dlg_respond(dlg, rdata, 500, NULL, &hdr_list, NULL);

	/* Can't terminate dialog because transaction is in progress.
	pjsip_dlg_terminate(dlg);
	 */
	goto on_return;
    }

    /* If account is locked to specific transport, then lock dialog
     * to this transport too.
     */
    if (pjsua_var.acc[acc_id].cfg.transport_id != PJSUA_INVALID_ID) {
	pjsip_tpselector tp_sel;

	pjsua_init_tpselector(pjsua_var.acc[acc_id].cfg.transport_id, &tp_sel);
	pjsip_dlg_set_transport(dlg, &tp_sel);
    }

    /* Create and attach pjsua_var data to the dialog */
    call->inv = inv;

    /* Store variables required for the callback after the async
     * media transport creation is completed.
     */
    call->async_call.dlg = dlg;
    pj_list_init(&call->async_call.call_var.inc_call.answers);

    /* Init media channel */
    status = pjsua_media_channel_init(call->index, PJSIP_ROLE_UAS, 
				      call->secure_level, 
				      rdata->tp_info.pool,
				      offer,
				      &sip_err_code, PJ_TRUE,
                                      &on_incoming_call_med_tp_complete);
    if (status == PJ_SUCCESS) {
        status = on_incoming_call_med_tp_complete(call_id, NULL);
        if (status != PJ_SUCCESS) {
            sip_err_code = PJSIP_SC_NOT_ACCEPTABLE;
            /* Since the call invite's state is still PJSIP_INV_STATE_NULL,
             * the invite session was not ended in
             * on_incoming_call_med_tp_complete(), so we need to send
             * a response message and terminate the invite here.
             */
            pjsip_dlg_respond(dlg, rdata, sip_err_code, NULL, NULL, NULL);
            pjsip_inv_terminate(call->inv, sip_err_code, PJ_FALSE); 
            call->inv = NULL; 
	    goto on_return;
        }
    } else if (status != PJ_EPENDING) {
	pjsua_perror(THIS_FILE, "Error initializing media channel", status);
	pjsip_dlg_respond(dlg, rdata, sip_err_code, NULL, NULL, NULL);
        pjsip_inv_terminate(call->inv, sip_err_code, PJ_FALSE); 
        call->inv = NULL; 
	goto on_return;
    }

    /* Create answer */
/*  
    status = pjsua_media_channel_create_sdp(call->index, rdata->tp_info.pool, 
					    offer, &answer, &sip_err_code);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error creating SDP answer", status);
	pjsip_endpt_respond(pjsua_var.endpt, NULL, rdata,
			    sip_err_code, NULL, NULL, NULL, NULL);
	goto on_return;
    }
*/

    /* Init Session Timers */
    status = pjsip_timer_init_session(inv, 
				    &pjsua_var.acc[acc_id].cfg.timer_setting);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Session Timer init failed", status);
        pjsip_dlg_respond(dlg, rdata, PJSIP_SC_INTERNAL_SERVER_ERROR, NULL, NULL, NULL);
	pjsip_inv_terminate(inv, PJSIP_SC_INTERNAL_SERVER_ERROR, PJ_FALSE);

	pjsua_media_channel_deinit(call->index);
	call->inv = NULL;

	goto on_return;
    }

    /* Update NAT type of remote endpoint, only when there is SDP in
     * incoming INVITE! 
     */
    if (pjsua_var.ua_cfg.nat_type_in_sdp && inv->neg &&
	pjmedia_sdp_neg_get_state(inv->neg) > PJMEDIA_SDP_NEG_STATE_LOCAL_OFFER) 
    {
	const pjmedia_sdp_session *remote_sdp;

	if (pjmedia_sdp_neg_get_neg_remote(inv->neg, &remote_sdp)==PJ_SUCCESS)
	    update_remote_nat_type(call, remote_sdp);
    }

    /* Must answer with some response to initial INVITE. We'll do this before
     * attaching the call to the invite session/dialog, so that the application
     * will not get notification about this event (on another scenario, it is
     * also possible that inv_send_msg() fails and causes the invite session to
     * be disconnected. If we have the call attached at this time, this will
     * cause the disconnection callback to be called before on_incoming_call()
     * callback is called, which is not right).
     */
    status = pjsip_inv_initial_answer(inv, rdata,
				      100, NULL, NULL, &response);
    if (status != PJ_SUCCESS) {
	if (response == NULL) {
	    pjsua_perror(THIS_FILE, "Unable to send answer to incoming INVITE",
			 status);
	    pjsip_dlg_respond(dlg, rdata, 500, NULL, NULL, NULL);
	    pjsip_inv_terminate(inv, 500, PJ_FALSE);
	} else {
	    pjsip_inv_send_msg(inv, response);
	    pjsip_inv_terminate(inv, response->msg->line.status.code,
				PJ_FALSE);
	}
	pjsua_media_channel_deinit(call->index);
	call->inv = NULL;
	goto on_return;

    } else {
	status = pjsip_inv_send_msg(inv, response);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to send 100 response", status);
	    pjsua_media_channel_deinit(call->index);
	    call->inv = NULL;
	    goto on_return;
	}
    }

    /* Only do this after sending 100/Trying (really! see the long comment
     * above)
     */
    dlg->mod_data[pjsua_var.mod.id] = call;
    inv->mod_data[pjsua_var.mod.id] = call;

    ++pjsua_var.call_cnt;

    /* Check if this request should replace existing call */
    if (replaced_dlg) {
	pjsip_inv_session *replaced_inv;
	struct pjsua_call *replaced_call;
	pjsip_tx_data *tdata;

	/* Get the invite session in the dialog */
	replaced_inv = pjsip_dlg_get_inv_session(replaced_dlg);

	/* Get the replaced call instance */
	replaced_call = (pjsua_call*) replaced_dlg->mod_data[pjsua_var.mod.id];

	/* Notify application */
	if (pjsua_var.ua_cfg.cb.on_call_replaced)
	    pjsua_var.ua_cfg.cb.on_call_replaced(replaced_call->index,
					         call_id);

	PJ_LOG(4,(THIS_FILE, "Answering replacement call %d with 200/OK",
			     call_id));

	/* Answer the new call with 200 response */
	status = pjsip_inv_answer(inv, 200, NULL, NULL, &tdata);
	if (status == PJ_SUCCESS)
	    status = pjsip_inv_send_msg(inv, tdata);

	if (status != PJ_SUCCESS)
	    pjsua_perror(THIS_FILE, "Error answering session", status);

	/* Note that inv may be invalid if 200/OK has caused error in
	 * starting the media.
	 */

	PJ_LOG(4,(THIS_FILE, "Disconnecting replaced call %d",
			     replaced_call->index));

	/* Disconnect replaced invite session */
	status = pjsip_inv_end_session(replaced_inv, PJSIP_SC_GONE, NULL,
				       &tdata);
	if (status == PJ_SUCCESS && tdata)
	    status = pjsip_inv_send_msg(replaced_inv, tdata);

	if (status != PJ_SUCCESS)
	    pjsua_perror(THIS_FILE, "Error terminating session", status);


    } else {

	/* Notify application if on_incoming_call() is overriden, 
	 * otherwise hangup the call with 480
	 */
	if (pjsua_var.ua_cfg.cb.on_incoming_call) {
	    pjsua_var.ua_cfg.cb.on_incoming_call(acc_id, call_id, rdata);
	} else {
	    pjsua_call_hangup(call_id, PJSIP_SC_TEMPORARILY_UNAVAILABLE, 
			      NULL, NULL);
	}
    }


    /* This INVITE request has been handled. */
on_return:
    pj_log_pop_indent();
    PJSUA_UNLOCK();
    return PJ_TRUE;
}



/*
 * Check if the specified call has active INVITE session and the INVITE
 * session has not been disconnected.
 */
PJ_DEF(pj_bool_t) pjsua_call_is_active(pjsua_call_id call_id)
{
    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);
    return pjsua_var.calls[call_id].inv != NULL &&
	   pjsua_var.calls[call_id].inv->state != PJSIP_INV_STATE_DISCONNECTED;
}


/* Acquire lock to the specified call_id */
pj_status_t acquire_call(const char *title,
				pjsua_call_id call_id,
				pjsua_call **p_call,
				pjsip_dialog **p_dlg)
{
    unsigned retry;
    pjsua_call *call = NULL;
    pj_bool_t has_pjsua_lock = PJ_FALSE;
    pj_status_t status = PJ_SUCCESS;
    pj_time_val time_start, timeout;

    pj_gettimeofday(&time_start);
    timeout.sec = 0;
    timeout.msec = PJSUA_ACQUIRE_CALL_TIMEOUT;
    pj_time_val_normalize(&timeout);

    for (retry=0; ; ++retry) {

        if (retry % 10 == 9) {
            pj_time_val dtime;

            pj_gettimeofday(&dtime);
            PJ_TIME_VAL_SUB(dtime, time_start);
            if (!PJ_TIME_VAL_LT(dtime, timeout))
                break;
        }
	
	has_pjsua_lock = PJ_FALSE;

	status = PJSUA_TRY_LOCK();
	if (status != PJ_SUCCESS) {
	    pj_thread_sleep(retry/10);
	    continue;
	}

	has_pjsua_lock = PJ_TRUE;
	call = &pjsua_var.calls[call_id];

	if (call->inv == NULL) {
	    PJSUA_UNLOCK();
	    PJ_LOG(3,(THIS_FILE, "Invalid call_id %d in %s", call_id, title));
	    return PJSIP_ESESSIONTERMINATED;
	}

	status = pjsip_dlg_try_inc_lock(call->inv->dlg);
	if (status != PJ_SUCCESS) {
	    PJSUA_UNLOCK();
	    pj_thread_sleep(retry/10);
	    continue;
	}

	PJSUA_UNLOCK();

	break;
    }

    if (status != PJ_SUCCESS) {
	if (has_pjsua_lock == PJ_FALSE)
	    PJ_LOG(1,(THIS_FILE, "Timed-out trying to acquire PJSUA mutex "
				 "(possibly system has deadlocked) in %s",
				 title));
	else
	    PJ_LOG(1,(THIS_FILE, "Timed-out trying to acquire dialog mutex "
				 "(possibly system has deadlocked) in %s",
				 title));
	return PJ_ETIMEDOUT;
    }
    
    *p_call = call;
    *p_dlg = call->inv->dlg;

    return PJ_SUCCESS;
}


/*
 * Obtain detail information about the specified call.
 */
PJ_DEF(pj_status_t) pjsua_call_get_info( pjsua_call_id call_id,
					 pjsua_call_info *info)
{
    pjsua_call *call;
    pjsip_dialog *dlg;
    unsigned mi;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    pj_bzero(info, sizeof(*info));

    /* Use PJSUA_LOCK() instead of acquire_call():
     *  https://trac.pjsip.org/repos/ticket/1371
     */
    PJSUA_LOCK();

    call = &pjsua_var.calls[call_id];
    dlg = (call->inv ? call->inv->dlg : call->async_call.dlg);
    if (!dlg) {
	PJSUA_UNLOCK();
	return PJSIP_ESESSIONTERMINATED;
    }

    /* id and role */
    info->id = call_id;
    info->role = dlg->role;
    info->acc_id = call->acc_id;

    /* local info */
    info->local_info.ptr = info->buf_.local_info;
    pj_strncpy(&info->local_info, &dlg->local.info_str,
	       sizeof(info->buf_.local_info));

    /* local contact */
    info->local_contact.ptr = info->buf_.local_contact;
    info->local_contact.slen = pjsip_uri_print(PJSIP_URI_IN_CONTACT_HDR,
					       dlg->local.contact->uri,
					       info->local_contact.ptr,
					       sizeof(info->buf_.local_contact));

    /* remote info */
    info->remote_info.ptr = info->buf_.remote_info;
    pj_strncpy(&info->remote_info, &dlg->remote.info_str,
	       sizeof(info->buf_.remote_info));

    /* remote contact */
    if (dlg->remote.contact) {
	int len;
	info->remote_contact.ptr = info->buf_.remote_contact;
	len = pjsip_uri_print(PJSIP_URI_IN_CONTACT_HDR,
			      dlg->remote.contact->uri,
			      info->remote_contact.ptr,
			      sizeof(info->buf_.remote_contact));
	if (len < 0) len = 0;
	info->remote_contact.slen = len;
    } else {
	info->remote_contact.slen = 0;
    }

    /* call id */
    info->call_id.ptr = info->buf_.call_id;
    pj_strncpy(&info->call_id, &dlg->call_id->id,
	       sizeof(info->buf_.call_id));

    /* call setting */
    pj_memcpy(&info->setting, &call->opt, sizeof(call->opt));

    /* state, state_text */
    if (call->inv) {
        info->state = call->inv->state;
    } else if (call->async_call.dlg && call->last_code==0) {
        info->state = PJSIP_INV_STATE_NULL;
    } else {
        info->state = PJSIP_INV_STATE_DISCONNECTED;
    }
    info->state_text = pj_str((char*)pjsip_inv_state_name(info->state));

    /* If call is disconnected, set the last_status from the cause code */
    if (call->inv && call->inv->state >= PJSIP_INV_STATE_DISCONNECTED) {
	/* last_status, last_status_text */
	info->last_status = call->inv->cause;

	info->last_status_text.ptr = info->buf_.last_status_text;
	pj_strncpy(&info->last_status_text, &call->inv->cause_text,
		   sizeof(info->buf_.last_status_text));
    } else {
	/* last_status, last_status_text */
	info->last_status = call->last_code;

	info->last_status_text.ptr = info->buf_.last_status_text;
	pj_strncpy(&info->last_status_text, &call->last_text,
		   sizeof(info->buf_.last_status_text));
    }
    
    /* Audio & video count offered by remote */
    info->rem_offerer   = call->rem_offerer;
    if (call->rem_offerer) {
	info->rem_aud_cnt = call->rem_aud_cnt;
	info->rem_vid_cnt = call->rem_vid_cnt;
    }

    /* Build array of media status and dir */
    info->media_cnt = 0;
    for (mi=0; mi < call->med_cnt &&
	       info->media_cnt < PJ_ARRAY_SIZE(info->media); ++mi)
    {
	pjsua_call_media *call_med = &call->media[mi];

	info->media[info->media_cnt].index = mi;
	info->media[info->media_cnt].status = call_med->state;
	info->media[info->media_cnt].dir = call_med->dir;
	info->media[info->media_cnt].type = call_med->type;

	if (call_med->type == PJMEDIA_TYPE_AUDIO) {
	    info->media[info->media_cnt].stream.aud.conf_slot =
						call_med->strm.a.conf_slot;
	} else if (call_med->type == PJMEDIA_TYPE_VIDEO) {
	    pjmedia_vid_dev_index cap_dev = PJMEDIA_VID_INVALID_DEV;

	    info->media[info->media_cnt].stream.vid.win_in = 
						call_med->strm.v.rdr_win_id;

	    if (call_med->strm.v.cap_win_id != PJSUA_INVALID_ID) {
		cap_dev = call_med->strm.v.cap_dev;
	    }
	    info->media[info->media_cnt].stream.vid.cap_dev = cap_dev;
	} else {
	    continue;
	}
	++info->media_cnt;
    }

    if (call->audio_idx != -1) {
	info->media_status = call->media[call->audio_idx].state;
	info->media_dir = call->media[call->audio_idx].dir;
	info->conf_slot = call->media[call->audio_idx].strm.a.conf_slot;
    }

    /* calculate duration */
    if (info->state >= PJSIP_INV_STATE_DISCONNECTED) {

	info->total_duration = call->dis_time;
	PJ_TIME_VAL_SUB(info->total_duration, call->start_time);

	if (call->conn_time.sec) {
	    info->connect_duration = call->dis_time;
	    PJ_TIME_VAL_SUB(info->connect_duration, call->conn_time);
	}

    } else if (info->state == PJSIP_INV_STATE_CONFIRMED) {

	pj_gettimeofday(&info->total_duration);
	PJ_TIME_VAL_SUB(info->total_duration, call->start_time);

	pj_gettimeofday(&info->connect_duration);
	PJ_TIME_VAL_SUB(info->connect_duration, call->conn_time);

    } else {
	pj_gettimeofday(&info->total_duration);
	PJ_TIME_VAL_SUB(info->total_duration, call->start_time);
    }

    PJSUA_UNLOCK();

    return PJ_SUCCESS;
}

/*
 * Check if call remote peer support the specified capability.
 */
PJ_DEF(pjsip_dialog_cap_status) pjsua_call_remote_has_cap(
						    pjsua_call_id call_id,
						    int htype,
						    const pj_str_t *hname,
						    const pj_str_t *token)
{
    pjsua_call *call;
    pjsip_dialog *dlg;
    pj_status_t status;
    pjsip_dialog_cap_status cap_status;

    status = acquire_call("pjsua_call_peer_has_cap()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	return PJSIP_DIALOG_CAP_UNKNOWN;

    cap_status = pjsip_dlg_remote_has_cap(dlg, htype, hname, token);

    pjsip_dlg_dec_lock(dlg);

    return cap_status;
}


/*
 * Attach application specific data to the call.
 */
PJ_DEF(pj_status_t) pjsua_call_set_user_data( pjsua_call_id call_id,
					      void *user_data)
{
    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);
    pjsua_var.calls[call_id].user_data = user_data;

    return PJ_SUCCESS;
}


/*
 * Get user data attached to the call.
 */
PJ_DEF(void*) pjsua_call_get_user_data(pjsua_call_id call_id)
{
    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     NULL);
    return pjsua_var.calls[call_id].user_data;
}


/*
 * Get remote's NAT type.
 */
PJ_DEF(pj_status_t) pjsua_call_get_rem_nat_type(pjsua_call_id call_id,
						pj_stun_nat_type *p_type)
{
    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(p_type != NULL, PJ_EINVAL);

    *p_type = pjsua_var.calls[call_id].rem_nat_type;
    return PJ_SUCCESS;
}


/*
 * Get media transport info for the specified media index.
 */
PJ_DEF(pj_status_t) 
pjsua_call_get_med_transport_info(pjsua_call_id call_id,
                                  unsigned med_idx,
                                  pjmedia_transport_info *t)
{
    pjsua_call *call;
    pjsua_call_media *call_med;
    pj_status_t status;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(t, PJ_EINVAL);

    PJSUA_LOCK();

    call = &pjsua_var.calls[call_id];
    
    if (med_idx >= call->med_cnt) {
	PJSUA_UNLOCK();
	return PJ_EINVAL;
    }

    call_med = &call->media[med_idx];

    pjmedia_transport_info_init(t);
    status = pjmedia_transport_get_info(call_med->tp, t);
    
    PJSUA_UNLOCK();
    return status;
}


/*
 * Send response to incoming INVITE request.
 */
PJ_DEF(pj_status_t) pjsua_call_answer( pjsua_call_id call_id, 
				       unsigned code,
				       const pj_str_t *reason,
				       const pjsua_msg_data *msg_data)
{
    return pjsua_call_answer2(call_id, NULL, code, reason, msg_data);
}


/*
 * Send response to incoming INVITE request.
 */
PJ_DEF(pj_status_t) pjsua_call_answer2(pjsua_call_id call_id,
				       const pjsua_call_setting *opt,
				       unsigned code,
				       const pj_str_t *reason,
				       const pjsua_msg_data *msg_data)
{
    pjsua_call *call;
    pjsip_dialog *dlg = NULL;
    pjsip_tx_data *tdata;
    pj_status_t status;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Answering call %d: code=%d", call_id, code));
    pj_log_push_indent();

    status = acquire_call("pjsua_call_answer()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	goto on_return;

    /* Apply call setting */
    status = apply_call_setting(call, opt, NULL);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Failed to apply call setting", status);
	goto on_return;
    }

    PJSUA_LOCK();
    /* If media transport creation is not yet completed, we will answer
     * the call in the media transport creation callback instead.
     */
    if (call->med_ch_cb) {
        struct call_answer *answer;
        
        PJ_LOG(4,(THIS_FILE, "Pending answering call %d upon completion "
                             "of media transport", call_id));

        answer = PJ_POOL_ZALLOC_T(call->inv->pool_prov, struct call_answer);
        answer->code = code;
        if (reason) {
            pj_strdup(call->inv->pool_prov, answer->reason, reason);
        }
        if (msg_data) {
            answer->msg_data = pjsua_msg_data_clone(call->inv->pool_prov,
                                                    msg_data);
        }
        pj_list_push_back(&call->async_call.call_var.inc_call.answers,
                          answer);

        PJSUA_UNLOCK();
        if (dlg) pjsip_dlg_dec_lock(dlg);
        pj_log_pop_indent();
        return status;
    }
    PJSUA_UNLOCK();

    if (call->res_time.sec == 0)
	pj_gettimeofday(&call->res_time);

    if (reason && reason->slen == 0)
	reason = NULL;

    /* Create response message */
    status = pjsip_inv_answer(call->inv, code, reason, NULL, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error creating response", 
		     status);
	goto on_return;
    }

    /* Call might have been disconnected if application is answering with
     * 200/OK and the media failed to start.
     */
    if (call->inv == NULL)
	goto on_return;

    /* Add additional headers etc */
    pjsua_process_msg_data( tdata, msg_data);

    /* Send the message */
    status = pjsip_inv_send_msg(call->inv, tdata);
    if (status != PJ_SUCCESS)
	pjsua_perror(THIS_FILE, "Error sending response", 
		     status);

on_return:
    if (dlg) pjsip_dlg_dec_lock(dlg);
    pj_log_pop_indent();
    return status;
}


/*
 * Hangup call by using method that is appropriate according to the
 * call state.
 */
PJ_DEF(pj_status_t) pjsua_call_hangup(pjsua_call_id call_id,
				      unsigned code,
				      const pj_str_t *reason,
				      const pjsua_msg_data *msg_data)
{
    pjsua_call *call;
    pjsip_dialog *dlg = NULL;
    pj_status_t status;
    pjsip_tx_data *tdata;


    if (call_id<0 || call_id>=(int)pjsua_var.ua_cfg.max_calls) {
	PJ_LOG(1,(THIS_FILE, "pjsua_call_hangup(): invalid call id %d",
			     call_id));
    }
    
    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Call %d hanging up: code=%d..", call_id, code));
    pj_log_push_indent();

    status = acquire_call("pjsua_call_hangup()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	goto on_return;

    if (code==0) {
	if (call->inv->state == PJSIP_INV_STATE_CONFIRMED)
	    code = PJSIP_SC_OK;
	else if (call->inv->role == PJSIP_ROLE_UAS)
	    code = PJSIP_SC_DECLINE;
	else
	    code = PJSIP_SC_REQUEST_TERMINATED;
    }

    status = pjsip_inv_end_session(call->inv, code, reason, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, 
		     "Failed to create end session message", 
		     status);
	goto on_return;
    }

    /* pjsip_inv_end_session may return PJ_SUCCESS with NULL 
     * as p_tdata when INVITE transaction has not been answered
     * with any provisional responses.
     */
    if (tdata == NULL)
	goto on_return;

    /* Add additional headers etc */
    pjsua_process_msg_data( tdata, msg_data);

    /* Send the message */
    status = pjsip_inv_send_msg(call->inv, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, 
		     "Failed to send end session message", 
		     status);
	goto on_return;
    }

    /* Stop lock codec timer, if it is active */
    if (call->lock_codec.reinv_timer.id) {
	pjsip_endpt_cancel_timer(pjsua_var.endpt, 
				 &call->lock_codec.reinv_timer);
	call->lock_codec.reinv_timer.id = PJ_FALSE;
    }

on_return:
    if (dlg) pjsip_dlg_dec_lock(dlg);
    pj_log_pop_indent();
    return status;
}


/*
 * Accept or reject redirection.
 */
PJ_DEF(pj_status_t) pjsua_call_process_redirect( pjsua_call_id call_id,
						 pjsip_redirect_op cmd)
{
    pjsua_call *call;
    pjsip_dialog *dlg;
    pj_status_t status;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    status = acquire_call("pjsua_call_process_redirect()", call_id, 
			  &call, &dlg);
    if (status != PJ_SUCCESS)
	return status;

    status = pjsip_inv_process_redirect(call->inv, cmd, NULL);

    pjsip_dlg_dec_lock(dlg);

    return status;
}


/*
 * Put the specified call on hold.
 */
PJ_DEF(pj_status_t) pjsua_call_set_hold(pjsua_call_id call_id,
					const pjsua_msg_data *msg_data)
{
    pjmedia_sdp_session *sdp;
    pjsua_call *call;
    pjsip_dialog *dlg = NULL;
    pjsip_tx_data *tdata;
    pj_status_t status;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Putting call %d on hold", call_id));
    pj_log_push_indent();

    status = acquire_call("pjsua_call_set_hold()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	goto on_return;

    if (call->inv->state != PJSIP_INV_STATE_CONFIRMED) {
	PJ_LOG(3,(THIS_FILE, "Can not hold call that is not confirmed"));
	status = PJSIP_ESESSIONSTATE;
	goto on_return;
    }

    status = create_sdp_of_call_hold(call, &sdp);
    if (status != PJ_SUCCESS)
	goto on_return;

    /* Create re-INVITE with new offer */
    status = pjsip_inv_reinvite( call->inv, NULL, sdp, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create re-INVITE", status);
	goto on_return;
    }

    /* Add additional headers etc */
    pjsua_process_msg_data( tdata, msg_data);

    /* Record the tx_data to keep track the operation */
    call->hold_msg = (void*) tdata;

    /* Send the request */
    status = pjsip_inv_send_msg( call->inv, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send re-INVITE", status);
	call->hold_msg = NULL;
	goto on_return;
    }

    /* Set flag that local put the call on hold */
    call->local_hold = PJ_TRUE;

on_return:
    if (dlg) pjsip_dlg_dec_lock(dlg);
    pj_log_pop_indent();
    return status;
}


/*
 * Send re-INVITE (to release hold).
 */
PJ_DEF(pj_status_t) pjsua_call_reinvite( pjsua_call_id call_id,
                                         unsigned options,
					 const pjsua_msg_data *msg_data)
{
    pjsua_call *call;
    pjsip_dialog *dlg = NULL;
    pj_status_t status;

    status = acquire_call("pjsua_call_reinvite()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	goto on_return;

    if (options != call->opt.flag)
	call->opt.flag = options;

    status = pjsua_call_reinvite2(call_id, NULL, msg_data);

on_return:
    if (dlg) pjsip_dlg_dec_lock(dlg);
    return status;
}


/*
 * Send re-INVITE (to release hold).
 */
PJ_DEF(pj_status_t) pjsua_call_reinvite2(pjsua_call_id call_id,
                                         const pjsua_call_setting *opt,
					 const pjsua_msg_data *msg_data)
{
    pjmedia_sdp_session *sdp;
    pj_str_t *new_contact = NULL;
    pjsip_tx_data *tdata;
    pjsua_call *call;
    pjsip_dialog *dlg = NULL;
    pj_status_t status;


    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Sending re-INVITE on call %d", call_id));
    pj_log_push_indent();

    status = acquire_call("pjsua_call_reinvite2()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	goto on_return;

    if (call->inv->state != PJSIP_INV_STATE_CONFIRMED) {
	PJ_LOG(3,(THIS_FILE, "Can not re-INVITE call that is not confirmed"));
	status = PJSIP_ESESSIONSTATE;
	goto on_return;
    }

    status = apply_call_setting(call, opt, NULL);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Failed to apply call setting", status);
	goto on_return;
    }

    /* Create SDP */
    if (call->local_hold && (call->opt.flag & PJSUA_CALL_UNHOLD)==0) {
	status = create_sdp_of_call_hold(call, &sdp);
    } else {
	status = pjsua_media_channel_create_sdp(call->index, 
						call->inv->pool_prov,
						NULL, &sdp, NULL);
	call->local_hold = PJ_FALSE;
    }
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to get SDP from media endpoint", 
		     status);
	goto on_return;
    }

    if ((call->opt.flag & PJSUA_CALL_UPDATE_CONTACT) &
	    pjsua_acc_is_valid(call->acc_id))
    {
	new_contact = &pjsua_var.acc[call->acc_id].contact;
    }

    /* Create re-INVITE with new offer */
    status = pjsip_inv_reinvite( call->inv, new_contact, sdp, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create re-INVITE", status);
	goto on_return;
    }

    /* Add additional headers etc */
    pjsua_process_msg_data( tdata, msg_data);

    /* Send the request */
    status = pjsip_inv_send_msg( call->inv, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send re-INVITE", status);
	goto on_return;
    }

on_return:
    if (dlg) pjsip_dlg_dec_lock(dlg);
    pj_log_pop_indent();
    return status;
}


/*
 * Send UPDATE request.
 */
PJ_DEF(pj_status_t) pjsua_call_update( pjsua_call_id call_id,
				       unsigned options,
				       const pjsua_msg_data *msg_data)
{
    pjsua_call *call;
    pjsip_dialog *dlg = NULL;
    pj_status_t status;

    status = acquire_call("pjsua_call_update()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	goto on_return;

    if (options != call->opt.flag)
	call->opt.flag = options;

    status = pjsua_call_update2(call_id, NULL, msg_data);

on_return:
    if (dlg) pjsip_dlg_dec_lock(dlg);
    return status;
}


/*
 * Send UPDATE request.
 */
PJ_DEF(pj_status_t) pjsua_call_update2(pjsua_call_id call_id,
				       const pjsua_call_setting *opt,
				       const pjsua_msg_data *msg_data)
{
    pjmedia_sdp_session *sdp;
    pj_str_t *new_contact = NULL;
    pjsip_tx_data *tdata;
    pjsua_call *call;
    pjsip_dialog *dlg = NULL;
    pj_status_t status;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Sending UPDATE on call %d", call_id));
    pj_log_push_indent();

    status = acquire_call("pjsua_call_update2()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	goto on_return;

    status = apply_call_setting(call, opt, NULL);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Failed to apply call setting", status);
	goto on_return;
    }

    /* Create SDP */
    status = pjsua_media_channel_create_sdp(call->index, 
					    call->inv->pool_prov, 
					    NULL, &sdp, NULL);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to get SDP from media endpoint", 
		     status);
	goto on_return;
    }

    if ((call->opt.flag & PJSUA_CALL_UPDATE_CONTACT) &
	    pjsua_acc_is_valid(call->acc_id))
    {
	new_contact = &pjsua_var.acc[call->acc_id].contact;
    }

    /* Create UPDATE with new offer */
    status = pjsip_inv_update(call->inv, new_contact, sdp, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create UPDATE request", status);
	goto on_return;
    }

    /* Add additional headers etc */
    pjsua_process_msg_data( tdata, msg_data);

    /* Send the request */
    status = pjsip_inv_send_msg( call->inv, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send UPDATE request", status);
	goto on_return;
    }

    call->local_hold = PJ_FALSE;

on_return:
    if (dlg) pjsip_dlg_dec_lock(dlg);
    pj_log_pop_indent();
    return status;
}


/*
 * Initiate call transfer to the specified address.
 */
PJ_DEF(pj_status_t) pjsua_call_xfer( pjsua_call_id call_id, 
				     const pj_str_t *dest,
				     const pjsua_msg_data *msg_data)
{
    pjsip_evsub *sub;
    pjsip_tx_data *tdata;
    pjsua_call *call;
    pjsip_dialog *dlg = NULL;
    pjsip_generic_string_hdr *gs_hdr;
    const pj_str_t str_ref_by = { "Referred-By", 11 };
    struct pjsip_evsub_user xfer_cb;
    pj_status_t status;


    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls &&
                     dest, PJ_EINVAL);
    
    PJ_LOG(4,(THIS_FILE, "Transfering call %d to %.*s", call_id,
			 (int)dest->slen, dest->ptr));
    pj_log_push_indent();

    status = acquire_call("pjsua_call_xfer()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	goto on_return;
   
    /* Create xfer client subscription. */
    pj_bzero(&xfer_cb, sizeof(xfer_cb));
    xfer_cb.on_evsub_state = &xfer_client_on_evsub_state;

    status = pjsip_xfer_create_uac(call->inv->dlg, &xfer_cb, &sub);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create xfer", status);
	goto on_return;
    }

    /* Associate this call with the client subscription */
    pjsip_evsub_set_mod_data(sub, pjsua_var.mod.id, call);

    /*
     * Create REFER request.
     */
    status = pjsip_xfer_initiate(sub, dest, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create REFER request", status);
	goto on_return;
    }

    /* Add Referred-By header */
    gs_hdr = pjsip_generic_string_hdr_create(tdata->pool, &str_ref_by,
					     &dlg->local.info_str);
    pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)gs_hdr);


    /* Add additional headers etc */
    pjsua_process_msg_data( tdata, msg_data);

    /* Send. */
    status = pjsip_xfer_send_request(sub, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send REFER request", status);
	goto on_return;
    }

    /* For simplicity (that's what this program is intended to be!), 
     * leave the original invite session as it is. More advanced application
     * may want to hold the INVITE, or terminate the invite, or whatever.
     */
on_return:
    if (dlg) pjsip_dlg_dec_lock(dlg);
    pj_log_pop_indent();
    return status;

}


/*
 * Initiate attended call transfer to the specified address.
 */
PJ_DEF(pj_status_t) pjsua_call_xfer_replaces( pjsua_call_id call_id, 
					      pjsua_call_id dest_call_id,
					      unsigned options,
					      const pjsua_msg_data *msg_data)
{
    pjsua_call *dest_call;
    pjsip_dialog *dest_dlg;
    char str_dest_buf[PJSIP_MAX_URL_SIZE*2];
    pj_str_t str_dest;
    int len;
    pjsip_uri *uri;
    pj_status_t status;
    

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(dest_call_id>=0 && 
		      dest_call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);
    
    PJ_LOG(4,(THIS_FILE, "Transfering call %d replacing with call %d",
			 call_id, dest_call_id));
    pj_log_push_indent();

    status = acquire_call("pjsua_call_xfer_replaces()", dest_call_id, 
			  &dest_call, &dest_dlg);
    if (status != PJ_SUCCESS) {
	pj_log_pop_indent();
	return status;
    }
        
    /* 
     * Create REFER destination URI with Replaces field.
     */

    /* Make sure we have sufficient buffer's length */
    PJ_ASSERT_ON_FAIL(dest_dlg->remote.info_str.slen +
		      dest_dlg->call_id->id.slen +
		      dest_dlg->remote.info->tag.slen +
		      dest_dlg->local.info->tag.slen + 32 
		      < (long)sizeof(str_dest_buf),
		      { status=PJSIP_EURITOOLONG; goto on_error; });

    /* Print URI */
    str_dest_buf[0] = '<';
    str_dest.slen = 1;

    uri = (pjsip_uri*) pjsip_uri_get_uri(dest_dlg->remote.info->uri);
    len = pjsip_uri_print(PJSIP_URI_IN_REQ_URI, uri, 
		          str_dest_buf+1, sizeof(str_dest_buf)-1);
    if (len < 0) {
	status = PJSIP_EURITOOLONG;
	goto on_error;
    }

    str_dest.slen += len;


    /* Build the URI */
    len = pj_ansi_snprintf(str_dest_buf + str_dest.slen, 
			   sizeof(str_dest_buf) - str_dest.slen,
			   "?%s"
			   "Replaces=%.*s"
			   "%%3Bto-tag%%3D%.*s"
			   "%%3Bfrom-tag%%3D%.*s>",
			   ((options&PJSUA_XFER_NO_REQUIRE_REPLACES) ?
			    "" : "Require=replaces&"),
			   (int)dest_dlg->call_id->id.slen,
			   dest_dlg->call_id->id.ptr,
			   (int)dest_dlg->remote.info->tag.slen,
			   dest_dlg->remote.info->tag.ptr,
			   (int)dest_dlg->local.info->tag.slen,
			   dest_dlg->local.info->tag.ptr);

    PJ_ASSERT_ON_FAIL(len > 0 && len <= (int)sizeof(str_dest_buf)-str_dest.slen,
		      { status=PJSIP_EURITOOLONG; goto on_error; });
    
    str_dest.ptr = str_dest_buf;
    str_dest.slen += len;

    pjsip_dlg_dec_lock(dest_dlg);
    
    status = pjsua_call_xfer(call_id, &str_dest, msg_data);

    pj_log_pop_indent();
    return status;

on_error:
    if (dest_dlg) pjsip_dlg_dec_lock(dest_dlg);
    pj_log_pop_indent();
    return status;
}


/**
 * Send instant messaging inside INVITE session.
 */
PJ_DEF(pj_status_t) pjsua_call_send_im( pjsua_call_id call_id, 
					const pj_str_t *mime_type,
					const pj_str_t *content,
					const pjsua_msg_data *msg_data,
					void *user_data)
{
    pjsua_call *call;
    pjsip_dialog *dlg = NULL;
    const pj_str_t mime_text_plain = pj_str("text/plain");
    pjsip_media_type ctype;
    pjsua_im_data *im_data;
    pjsip_tx_data *tdata;
    pj_status_t status;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Call %d sending %d bytes MESSAGE..",
        	          call_id, (int)content->slen));
    pj_log_push_indent();

    status = acquire_call("pjsua_call_send_im()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	goto on_return;
    
    /* Set default media type if none is specified */
    if (mime_type == NULL) {
	mime_type = &mime_text_plain;
    }

    /* Create request message. */
    status = pjsip_dlg_create_request( call->inv->dlg, &pjsip_message_method,
				       -1, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create MESSAGE request", status);
	goto on_return;
    }

    /* Add accept header. */
    pjsip_msg_add_hdr( tdata->msg, 
		       (pjsip_hdr*)pjsua_im_create_accept(tdata->pool));

    /* Parse MIME type */
    pjsua_parse_media_type(tdata->pool, mime_type, &ctype);

    /* Create "text/plain" message body. */
    tdata->msg->body = pjsip_msg_body_create( tdata->pool, &ctype.type,
					      &ctype.subtype, content);
    if (tdata->msg->body == NULL) {
	pjsua_perror(THIS_FILE, "Unable to create msg body", PJ_ENOMEM);
	pjsip_tx_data_dec_ref(tdata);
	goto on_return;
    }

    /* Add additional headers etc */
    pjsua_process_msg_data( tdata, msg_data);

    /* Create IM data and attach to the request. */
    im_data = PJ_POOL_ZALLOC_T(tdata->pool, pjsua_im_data);
    im_data->acc_id = call->acc_id;
    im_data->call_id = call_id;
    im_data->to = call->inv->dlg->remote.info_str;
    pj_strdup_with_null(tdata->pool, &im_data->body, content);
    im_data->user_data = user_data;


    /* Send the request. */
    status = pjsip_dlg_send_request( call->inv->dlg, tdata, 
				     pjsua_var.mod.id, im_data);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send MESSAGE request", status);
	goto on_return;
    }

on_return:
    if (dlg) pjsip_dlg_dec_lock(dlg);
    pj_log_pop_indent();
    return status;
}


/*
 * Send IM typing indication inside INVITE session.
 */
PJ_DEF(pj_status_t) pjsua_call_send_typing_ind( pjsua_call_id call_id, 
						pj_bool_t is_typing,
						const pjsua_msg_data*msg_data)
{
    pjsua_call *call;
    pjsip_dialog *dlg = NULL;
    pjsip_tx_data *tdata;
    pj_status_t status;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Call %d sending typing indication..",
            	          call_id));
    pj_log_push_indent();

    status = acquire_call("pjsua_call_send_typing_ind", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	goto on_return;

    /* Create request message. */
    status = pjsip_dlg_create_request( call->inv->dlg, &pjsip_message_method,
				       -1, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create MESSAGE request", status);
	goto on_return;
    }

    /* Create "application/im-iscomposing+xml" msg body. */
    tdata->msg->body = pjsip_iscomposing_create_body(tdata->pool, is_typing,
						     NULL, NULL, -1);

    /* Add additional headers etc */
    pjsua_process_msg_data( tdata, msg_data);

    /* Send the request. */
    status = pjsip_dlg_send_request( call->inv->dlg, tdata, -1, NULL);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send MESSAGE request", status);
	goto on_return;
    }

on_return:
    if (dlg) pjsip_dlg_dec_lock(dlg);
    pj_log_pop_indent();
    return status;
}


/*
 * Send arbitrary request.
 */
PJ_DEF(pj_status_t) pjsua_call_send_request(pjsua_call_id call_id,
					    const pj_str_t *method_str,
					    const pjsua_msg_data *msg_data)
{
    pjsua_call *call;
    pjsip_dialog *dlg = NULL;
    pjsip_method method;
    pjsip_tx_data *tdata;
    pj_status_t status;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Call %d sending %.*s request..",
            	          call_id, (int)method_str->slen, method_str->ptr));
    pj_log_push_indent();

    status = acquire_call("pjsua_call_send_request", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	goto on_return;

    /* Init method */
    pjsip_method_init_np(&method, (pj_str_t*)method_str);

    /* Create request message. */
    status = pjsip_dlg_create_request( call->inv->dlg, &method, -1, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create request", status);
	goto on_return;
    }

    /* Add additional headers etc */
    pjsua_process_msg_data( tdata, msg_data);

    /* Send the request. */
    status = pjsip_dlg_send_request( call->inv->dlg, tdata, -1, NULL);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send request", status);
	goto on_return;
    }

on_return:
    if (dlg) pjsip_dlg_dec_lock(dlg);
    pj_log_pop_indent();
    return status;
}


/*
 * Terminate all calls.
 */
PJ_DEF(void) pjsua_call_hangup_all(void)
{
    unsigned i;

    PJ_LOG(4,(THIS_FILE, "Hangup all calls.."));
    pj_log_push_indent();

    // This may deadlock, see https://trac.pjsip.org/repos/ticket/1305
    //PJSUA_LOCK();

    for (i=0; i<pjsua_var.ua_cfg.max_calls; ++i) {
	if (pjsua_var.calls[i].inv)
	    pjsua_call_hangup(i, 0, NULL, NULL);
    }

    //PJSUA_UNLOCK();
    pj_log_pop_indent();
}


/* Proto */
static pj_status_t perform_lock_codec(pjsua_call *call);

/* Timer callback to send re-INVITE or UPDATE to lock codec */
static void reinv_timer_cb(pj_timer_heap_t *th,
			   pj_timer_entry *entry)
{
    pjsua_call_id call_id = (pjsua_call_id)(pj_size_t)entry->user_data;
    pjsip_dialog *dlg;
    pjsua_call *call;
    pj_status_t status;

    PJ_UNUSED_ARG(th);

    pjsua_var.calls[call_id].lock_codec.reinv_timer.id = PJ_FALSE;

    status = acquire_call("reinv_timer_cb()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	return;

    status = perform_lock_codec(call);

    pjsip_dlg_dec_lock(dlg);
}


/* Check if the specified format can be skipped in counting codecs */
static pj_bool_t is_non_av_fmt(const pjmedia_sdp_media *m,
				  const pj_str_t *fmt)
{
    const pj_str_t STR_TEL = {"telephone-event", 15};
    unsigned pt;

    pt = pj_strtoul(fmt);

    /* Check for comfort noise */
    if (pt == PJMEDIA_RTP_PT_CN)
	return PJ_TRUE;

    /* Dynamic PT, check the format name */
    if (pt >= 96) {
	pjmedia_sdp_attr *a;
	pjmedia_sdp_rtpmap rtpmap;

	/* Get the format name */
	a = pjmedia_sdp_attr_find2(m->attr_count, m->attr, "rtpmap", fmt);
	if (a && pjmedia_sdp_attr_get_rtpmap(a, &rtpmap)==PJ_SUCCESS) {
	    /* Check for telephone-event */
	    if (pj_stricmp(&rtpmap.enc_name, &STR_TEL)==0)
		return PJ_TRUE;
	} else {
	    /* Invalid SDP, should not reach here */
	    pj_assert(!"SDP should have been validated!");
	    return PJ_TRUE;
	}
    }

    return PJ_FALSE;
}


/* Send re-INVITE or UPDATE with new SDP offer to select only one codec
 * out of several codecs presented by callee in his answer.
 */
static pj_status_t perform_lock_codec(pjsua_call *call)
{
    const pj_str_t STR_UPDATE = {"UPDATE", 6};
    const pjmedia_sdp_session *local_sdp = NULL, *new_sdp;
    unsigned i;
    pj_bool_t rem_can_update;
    pj_bool_t need_lock_codec = PJ_FALSE;
    pjsip_tx_data *tdata;
    pj_status_t status;

    PJ_ASSERT_RETURN(call->lock_codec.reinv_timer.id==PJ_FALSE,
		     PJ_EINVALIDOP);

    /* Verify if another SDP negotiation is in progress, e.g: session timer
     * or another re-INVITE.
     */
    if (call->inv==NULL || call->inv->neg==NULL ||
	pjmedia_sdp_neg_get_state(call->inv->neg)!=PJMEDIA_SDP_NEG_STATE_DONE)
    {
	return PJMEDIA_SDPNEG_EINSTATE;
    }

    /* Don't do this if call is disconnecting! */
    if (call->inv->state > PJSIP_INV_STATE_CONFIRMED ||
	call->inv->cause >= 200)
    {
	return PJ_EINVALIDOP;
    }

    /* Verify if another SDP negotiation has been completed by comparing
     * the SDP version.
     */
    status = pjmedia_sdp_neg_get_active_local(call->inv->neg, &local_sdp);
    if (status != PJ_SUCCESS)
	return status;
    if (local_sdp->origin.version > call->lock_codec.sdp_ver)
	return PJMEDIA_SDP_EINVER;

    PJ_LOG(3, (THIS_FILE, "Updating media session to use only one codec.."));

    /* Update the new offer so it contains only a codec. Note that formats
     * order in the offer should have been matched to the answer, so we can
     * just directly update the offer without looking-up the answer.
     */
    new_sdp = pjmedia_sdp_session_clone(call->inv->pool_prov, local_sdp);

    for (i = 0; i < call->med_cnt; ++i) {
	unsigned j = 0, codec_cnt = 0;
	const pjmedia_sdp_media *ref_m;
	pjmedia_sdp_media *m;
	pjsua_call_media *call_med = &call->media[i];

	/* Verify if media is deactivated */
	if (call_med->state == PJSUA_CALL_MEDIA_NONE ||
	    call_med->state == PJSUA_CALL_MEDIA_ERROR ||
	    call_med->dir == PJMEDIA_DIR_NONE)
	{
	    continue;
	}

	ref_m = local_sdp->media[i];
	m = new_sdp->media[i];

	/* Verify that media must be active. */
	pj_assert(ref_m->desc.port);

	while (j < m->desc.fmt_count) {
	    pjmedia_sdp_attr *a;
	    pj_str_t *fmt = &m->desc.fmt[j];

	    if (is_non_av_fmt(m, fmt) || (++codec_cnt == 1)) {
		++j;
		continue;
	    }

	    /* Remove format */
	    a = pjmedia_sdp_attr_find2(m->attr_count, m->attr, "rtpmap", fmt);
	    if (a) pjmedia_sdp_attr_remove(&m->attr_count, m->attr, a);
	    a = pjmedia_sdp_attr_find2(m->attr_count, m->attr, "fmtp", fmt);
	    if (a) pjmedia_sdp_attr_remove(&m->attr_count, m->attr, a);
	    pj_array_erase(m->desc.fmt, sizeof(m->desc.fmt[0]),
			   m->desc.fmt_count, j);
	    --m->desc.fmt_count;
	}
	
	need_lock_codec |= (ref_m->desc.fmt_count > m->desc.fmt_count);
    }

    /* Last check if SDP trully needs to be updated. It is possible that OA
     * negotiations have completed and SDP has changed but we didn't
     * increase the SDP version (should not happen!).
     */
    if (!need_lock_codec)
	return PJ_SUCCESS;

    /* Send UPDATE or re-INVITE */
    rem_can_update = pjsip_dlg_remote_has_cap(call->inv->dlg,
					      PJSIP_H_ALLOW,
					      NULL, &STR_UPDATE) ==
						PJSIP_DIALOG_CAP_SUPPORTED;
    if (rem_can_update) {
	status = pjsip_inv_update(call->inv, NULL, new_sdp, &tdata);
    } else {
	status = pjsip_inv_reinvite(call->inv, NULL, new_sdp, &tdata);
    }

    if (status==PJ_EINVALIDOP &&
	++call->lock_codec.retry_cnt <= LOCK_CODEC_MAX_RETRY)
    {
	/* Ups, let's reschedule again */
	pj_time_val delay = {0, LOCK_CODEC_RETRY_INTERVAL};
	pj_time_val_normalize(&delay);
	call->lock_codec.reinv_timer.id = PJ_TRUE;
	pjsip_endpt_schedule_timer(pjsua_var.endpt,
				   &call->lock_codec.reinv_timer, &delay);
	return status;
    } else if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error creating UPDATE/re-INVITE to lock codec",
		     status);
	return status;
    }

    /* Send the UPDATE/re-INVITE request */
    status = pjsip_inv_send_msg(call->inv, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error sending UPDATE/re-INVITE in lock codec",
		     status);
	return status;
    }

    return status;
}

/* Check if remote answerer has given us more than one codecs. If so,
 * create another offer with one codec only to lock down the codec.
 */
static pj_status_t lock_codec(pjsua_call *call)
{
    pjsip_inv_session *inv = call->inv;
    const pjmedia_sdp_session *local_sdp, *remote_sdp;
    pj_time_val delay = {0, 0};
    const pj_str_t st_update = {"UPDATE", 6};
    unsigned i;
    pj_bool_t has_mult_fmt = PJ_FALSE;
    pj_status_t status;

    /* Stop lock codec timer, if it is active */
    if (call->lock_codec.reinv_timer.id) {
	pjsip_endpt_cancel_timer(pjsua_var.endpt,
				 &call->lock_codec.reinv_timer);
	call->lock_codec.reinv_timer.id = PJ_FALSE;
    }

    /* Skip this if we are the answerer */
    if (!inv->neg || !pjmedia_sdp_neg_was_answer_remote(inv->neg)) {
        return PJ_SUCCESS;
    }

    /* Delay this when the SDP negotiation done in call state EARLY and
     * remote does not support UPDATE method.
     */
    if (inv->state == PJSIP_INV_STATE_EARLY && 
	pjsip_dlg_remote_has_cap(inv->dlg, PJSIP_H_ALLOW, NULL, &st_update)!=
	PJSIP_DIALOG_CAP_SUPPORTED)
    {
        call->lock_codec.pending = PJ_TRUE;
        return PJ_SUCCESS;
    }

    status = pjmedia_sdp_neg_get_active_local(inv->neg, &local_sdp);
    if (status != PJ_SUCCESS)
	return status;
    status = pjmedia_sdp_neg_get_active_remote(inv->neg, &remote_sdp);
    if (status != PJ_SUCCESS)
	return status;

    /* Find multiple codecs answer in all media */
    for (i = 0; i < call->med_cnt; ++i) {
	pjsua_call_media *call_med = &call->media[i];
	const pjmedia_sdp_media *rem_m, *loc_m;
	unsigned codec_cnt = 0;

	/* Skip this if the media is inactive or error */
	if (call_med->state == PJSUA_CALL_MEDIA_NONE ||
	    call_med->state == PJSUA_CALL_MEDIA_ERROR ||
	    call_med->dir == PJMEDIA_DIR_NONE)
	{
	    continue;
	}

	/* Remote may answer with less media lines. */
	if (i >= remote_sdp->media_count)
	    continue;

	rem_m = remote_sdp->media[i];
	loc_m = local_sdp->media[i];

	/* Verify that media must be active. */
	pj_assert(loc_m->desc.port && rem_m->desc.port);

	/* Count the formats in the answer. */
	if (rem_m->desc.fmt_count==1) {
	    codec_cnt = 1;
	} else {
	    unsigned j;
	    for (j=0; j<rem_m->desc.fmt_count && codec_cnt <= 1; ++j) {
		if (!is_non_av_fmt(rem_m, &rem_m->desc.fmt[j]))
		    ++codec_cnt;
	    }
	}

	if (codec_cnt > 1) {
	    has_mult_fmt = PJ_TRUE;
	    break;
	}
    }

    /* Each media in the answer already contains single codec. */
    if (!has_mult_fmt) {
	call->lock_codec.retry_cnt = 0;
	return PJ_SUCCESS;
    }

    /* Remote keeps answering with multiple codecs, let's just give up
     * locking codec to avoid infinite retry loop.
     */
    if (++call->lock_codec.retry_cnt > LOCK_CODEC_MAX_RETRY)
        return PJ_SUCCESS;

    PJ_LOG(4, (THIS_FILE, "Got answer with multiple codecs, scheduling "
			  "updating media session to use only one codec.."));

    call->lock_codec.sdp_ver = local_sdp->origin.version;

    /* Can't send UPDATE or re-INVITE now, so just schedule it immediately.
     * See: https://trac.pjsip.org/repos/ticket/1149
     */
    pj_timer_entry_init(&call->lock_codec.reinv_timer, PJ_TRUE,
			(void*)(pj_size_t)call->index,
			&reinv_timer_cb);
    pjsip_endpt_schedule_timer(pjsua_var.endpt,
			       &call->lock_codec.reinv_timer, &delay);

    return PJ_SUCCESS;
}

/*
 * This callback receives notification from invite session when the
 * session state has changed.
 */
static void pjsua_call_on_state_changed(pjsip_inv_session *inv, 
					pjsip_event *e)
{
    pjsua_call *call;

    pj_log_push_indent();

    call = (pjsua_call*) inv->dlg->mod_data[pjsua_var.mod.id];

    if (!call) {
	pj_log_pop_indent();
	return;
    }


    /* Get call times */
    switch (inv->state) {
	case PJSIP_INV_STATE_EARLY:
	case PJSIP_INV_STATE_CONNECTING:
	    if (call->res_time.sec == 0)
		pj_gettimeofday(&call->res_time);
	    call->last_code = (pjsip_status_code) 
	    		      e->body.tsx_state.tsx->status_code;
	    pj_strncpy(&call->last_text, 
		       &e->body.tsx_state.tsx->status_text,
		       sizeof(call->last_text_buf_));
	    break;
	case PJSIP_INV_STATE_CONFIRMED:
	    pj_gettimeofday(&call->conn_time);

            /* See if lock codec was pended as media update was done in the
             * EARLY state and remote does not support UPDATE.
             */
            if (call->lock_codec.pending) {
		pj_status_t status;
		status = lock_codec(call);
		if (status != PJ_SUCCESS) {
		    pjsua_perror(THIS_FILE, "Unable to lock codec", status);
                }
                call->lock_codec.pending = PJ_FALSE;
	    }
	    break;
	case PJSIP_INV_STATE_DISCONNECTED:
	    pj_gettimeofday(&call->dis_time);
	    if (call->res_time.sec == 0)
		pj_gettimeofday(&call->res_time);
	    if (e->type == PJSIP_EVENT_TSX_STATE && 
		e->body.tsx_state.tsx->status_code > call->last_code) 
	    {
		call->last_code = (pjsip_status_code) 
				  e->body.tsx_state.tsx->status_code;
		pj_strncpy(&call->last_text, 
			   &e->body.tsx_state.tsx->status_text,
			   sizeof(call->last_text_buf_));
	    } else {
		call->last_code = PJSIP_SC_REQUEST_TERMINATED;
		pj_strncpy(&call->last_text,
			   pjsip_get_status_text(call->last_code),
			   sizeof(call->last_text_buf_));
	    }

	    /* Stop lock codec timer, if it is active */
	    if (call->lock_codec.reinv_timer.id) {
		pjsip_endpt_cancel_timer(pjsua_var.endpt, 
					 &call->lock_codec.reinv_timer);
		call->lock_codec.reinv_timer.id = PJ_FALSE;
	    }
	    break;
	default:
	    call->last_code = (pjsip_status_code) 
	    		      e->body.tsx_state.tsx->status_code;
	    pj_strncpy(&call->last_text, 
		       &e->body.tsx_state.tsx->status_text,
		       sizeof(call->last_text_buf_));
	    break;
    }

    /* If this is an outgoing INVITE that was created because of
     * REFER/transfer, send NOTIFY to transferer.
     */
    if (call->xfer_sub && e->type==PJSIP_EVENT_TSX_STATE)  {
	int st_code = -1;
	pjsip_evsub_state ev_state = PJSIP_EVSUB_STATE_ACTIVE;
	

	switch (call->inv->state) {
	case PJSIP_INV_STATE_NULL:
	case PJSIP_INV_STATE_CALLING:
	    /* Do nothing */
	    break;

	case PJSIP_INV_STATE_EARLY:
	case PJSIP_INV_STATE_CONNECTING:
	    st_code = e->body.tsx_state.tsx->status_code;
	    if (call->inv->state == PJSIP_INV_STATE_CONNECTING)
		ev_state = PJSIP_EVSUB_STATE_TERMINATED;
	    else
		ev_state = PJSIP_EVSUB_STATE_ACTIVE;
	    break;

	case PJSIP_INV_STATE_CONFIRMED:
#if 0
/* We don't need this, as we've terminated the subscription in
 * CONNECTING state.
 */
	    /* When state is confirmed, send the final 200/OK and terminate
	     * subscription.
	     */
	    st_code = e->body.tsx_state.tsx->status_code;
	    ev_state = PJSIP_EVSUB_STATE_TERMINATED;
#endif
	    break;

	case PJSIP_INV_STATE_DISCONNECTED:
	    st_code = e->body.tsx_state.tsx->status_code;
	    ev_state = PJSIP_EVSUB_STATE_TERMINATED;
	    break;

	case PJSIP_INV_STATE_INCOMING:
	    /* Nothing to do. Just to keep gcc from complaining about
	     * unused enums.
	     */
	    break;
	}

	if (st_code != -1) {
	    pjsip_tx_data *tdata;
	    pj_status_t status;

	    status = pjsip_xfer_notify( call->xfer_sub,
					ev_state, st_code,
					NULL, &tdata);
	    if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "Unable to create NOTIFY", status);
	    } else {
		status = pjsip_xfer_send_request(call->xfer_sub, tdata);
		if (status != PJ_SUCCESS) {
		    pjsua_perror(THIS_FILE, "Unable to send NOTIFY", status);
		}
	    }
	}
    }


    if (pjsua_var.ua_cfg.cb.on_call_state)
	(*pjsua_var.ua_cfg.cb.on_call_state)(call->index, e);

    /* call->inv may be NULL now */

    /* Destroy media session when invite session is disconnected. */
    if (inv->state == PJSIP_INV_STATE_DISCONNECTED) {

	PJSUA_LOCK();
	
	pjsua_media_channel_deinit(call->index);

	/* Free call */
	call->inv = NULL;

	pj_assert(pjsua_var.call_cnt > 0);
	--pjsua_var.call_cnt;

	/* Reset call */
	reset_call(call->index);

	PJSUA_UNLOCK();
    }
    pj_log_pop_indent();
}

/*
 * This callback is called by invite session framework when UAC session
 * has forked.
 */
static void pjsua_call_on_forked( pjsip_inv_session *inv, 
				  pjsip_event *e)
{
    PJ_UNUSED_ARG(inv);
    PJ_UNUSED_ARG(e);

    PJ_TODO(HANDLE_FORKED_DIALOG);
}


/*
 * Callback from UA layer when forked dialog response is received.
 */
pjsip_dialog* on_dlg_forked(pjsip_dialog *dlg, pjsip_rx_data *res)
{
    if (dlg->uac_has_2xx && 
	res->msg_info.cseq->method.id == PJSIP_INVITE_METHOD &&
	pjsip_rdata_get_tsx(res) == NULL &&
	res->msg_info.msg->line.status.code/100 == 2) 
    {
	pjsip_dialog *forked_dlg;
	pjsip_tx_data *bye;
	pj_status_t status;

	/* Create forked dialog */
	status = pjsip_dlg_fork(dlg, res, &forked_dlg);
	if (status != PJ_SUCCESS)
	    return NULL;

	pjsip_dlg_inc_lock(forked_dlg);

	/* Disconnect the call */
	status = pjsip_dlg_create_request(forked_dlg, &pjsip_bye_method,
					  -1, &bye);
	if (status == PJ_SUCCESS) {
	    status = pjsip_dlg_send_request(forked_dlg, bye, -1, NULL);
	}

	pjsip_dlg_dec_lock(forked_dlg);

	if (status != PJ_SUCCESS) {
	    return NULL;
	}

	return forked_dlg;

    } else {
	return dlg;
    }
}

/*
 * Disconnect call upon error.
 */
static void call_disconnect( pjsip_inv_session *inv, 
			     int code )
{
    pjsua_call *call;
    pjsip_tx_data *tdata;
    pj_status_t status;

    call = (pjsua_call*) inv->dlg->mod_data[pjsua_var.mod.id];

    status = pjsip_inv_end_session(inv, code, NULL, &tdata);
    if (status != PJ_SUCCESS)
	return;

    /* Add SDP in 488 status */
#if DISABLED_FOR_TICKET_1185
    if (call && call->tp && tdata->msg->type==PJSIP_RESPONSE_MSG &&
	code==PJSIP_SC_NOT_ACCEPTABLE_HERE) 
    {
	pjmedia_sdp_session *local_sdp;
	pjmedia_transport_info ti;

	pjmedia_transport_info_init(&ti);
	pjmedia_transport_get_info(call->med_tp, &ti);
	status = pjmedia_endpt_create_sdp(pjsua_var.med_endpt, tdata->pool, 
					  1, &ti.sock_info, &local_sdp);
	if (status == PJ_SUCCESS) {
	    pjsip_create_sdp_body(tdata->pool, local_sdp,
				  &tdata->msg->body);
	}
    }
#endif

    pjsip_inv_send_msg(inv, tdata);
}

/*
 * Callback to be called when SDP offer/answer negotiation has just completed
 * in the session. This function will start/update media if negotiation
 * has succeeded.
 */
static void pjsua_call_on_media_update(pjsip_inv_session *inv,
				       pj_status_t status)
{
    pjsua_call *call;
    const pjmedia_sdp_session *local_sdp;
    const pjmedia_sdp_session *remote_sdp;
    //const pj_str_t st_update = {"UPDATE", 6};

    pj_log_push_indent();

    call = (pjsua_call*) inv->dlg->mod_data[pjsua_var.mod.id];

    if (status != PJ_SUCCESS) {

	pjsua_perror(THIS_FILE, "SDP negotiation has failed", status);

	/* Clean up provisional media */
	pjsua_media_prov_clean_up(call->index);

	/* Do not deinitialize media since this may be a re-INVITE or
	 * UPDATE (which in this case the media should not get affected
	 * by the failed re-INVITE/UPDATE). The media will be shutdown
	 * when call is disconnected anyway.
	 */
	/* Stop/destroy media, if any */
	/*pjsua_media_channel_deinit(call->index);*/

	/* Disconnect call if we're not in the middle of initializing an
	 * UAS dialog and if this is not a re-INVITE 
	 */
	if (inv->state != PJSIP_INV_STATE_NULL &&
	    inv->state != PJSIP_INV_STATE_CONFIRMED) 
	{
	    call_disconnect(inv, PJSIP_SC_UNSUPPORTED_MEDIA_TYPE);
	}

	goto on_return;
    }


    /* Get local and remote SDP */
    status = pjmedia_sdp_neg_get_active_local(call->inv->neg, &local_sdp);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, 
		     "Unable to retrieve currently active local SDP", 
		     status);
	//call_disconnect(inv, PJSIP_SC_UNSUPPORTED_MEDIA_TYPE);
	goto on_return;
    }

    status = pjmedia_sdp_neg_get_active_remote(call->inv->neg, &remote_sdp);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, 
		     "Unable to retrieve currently active remote SDP", 
		     status);
	//call_disconnect(inv, PJSIP_SC_UNSUPPORTED_MEDIA_TYPE);
	goto on_return;
    }

    /* Update remote's NAT type */
    if (pjsua_var.ua_cfg.nat_type_in_sdp) {
	update_remote_nat_type(call, remote_sdp);
    }

    /* Update media channel with the new SDP */
    status = pjsua_media_channel_update(call->index, local_sdp, remote_sdp);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create media session", 
		     status);
	call_disconnect(inv, PJSIP_SC_NOT_ACCEPTABLE_HERE);
	/* No need to deinitialize; media will be shutdown when call
	 * state is disconnected anyway.
	 */
	/*pjsua_media_channel_deinit(call->index);*/
	goto on_return;
    }

    /* Ticket #476: make sure only one codec is specified in the answer. */
    status = lock_codec(call);
    if (status != PJ_SUCCESS) {
        pjsua_perror(THIS_FILE, "Unable to lock codec", status);
    }

    /* Call application callback, if any */
    if (pjsua_var.ua_cfg.cb.on_call_media_state)
	pjsua_var.ua_cfg.cb.on_call_media_state(call->index);

on_return:
    pj_log_pop_indent();
}


/* Modify SDP for call hold. */
static pj_status_t modify_sdp_of_call_hold(pjsua_call *call,
					   pj_pool_t *pool,
					   pjmedia_sdp_session *sdp)
{
    unsigned mi;

    /* Call-hold is done by set the media direction to 'sendonly' 
     * (PJMEDIA_DIR_ENCODING), except when current media direction is 
     * 'inactive' (PJMEDIA_DIR_NONE).
     * (See RFC 3264 Section 8.4 and RFC 4317 Section 3.1)
     */
    /* http://trac.pjsip.org/repos/ticket/880 
       if (call->dir != PJMEDIA_DIR_ENCODING) {
     */
    /* https://trac.pjsip.org/repos/ticket/1142:
     *  configuration to use c=0.0.0.0 for call hold.
     */

    for (mi=0; mi<sdp->media_count; ++mi) {
	pjmedia_sdp_media *m = sdp->media[mi];

	if (call->call_hold_type == PJSUA_CALL_HOLD_TYPE_RFC2543) {
	    pjmedia_sdp_conn *conn;
	    pjmedia_sdp_attr *attr;

	    /* Get SDP media connection line */
	    conn = m->conn;
	    if (!conn)
		conn = sdp->conn;

	    /* Modify address */
	    conn->addr = pj_str("0.0.0.0");

	    /* Remove existing directions attributes */
	    pjmedia_sdp_media_remove_all_attr(m, "sendrecv");
	    pjmedia_sdp_media_remove_all_attr(m, "sendonly");
	    pjmedia_sdp_media_remove_all_attr(m, "recvonly");
	    pjmedia_sdp_media_remove_all_attr(m, "inactive");

	    /* Add inactive attribute */
	    attr = pjmedia_sdp_attr_create(pool, "inactive", NULL);
	    pjmedia_sdp_media_add_attr(m, attr);


	} else {
	    pjmedia_sdp_attr *attr;

	    /* Remove existing directions attributes */
	    pjmedia_sdp_media_remove_all_attr(m, "sendrecv");
	    pjmedia_sdp_media_remove_all_attr(m, "sendonly");
	    pjmedia_sdp_media_remove_all_attr(m, "recvonly");
	    pjmedia_sdp_media_remove_all_attr(m, "inactive");

	    if (call->media[mi].dir & PJMEDIA_DIR_ENCODING) {
		/* Add sendonly attribute */
		attr = pjmedia_sdp_attr_create(pool, "sendonly", NULL);
		pjmedia_sdp_media_add_attr(m, attr);
	    } else {
		/* Add inactive attribute */
		attr = pjmedia_sdp_attr_create(pool, "inactive", NULL);
		pjmedia_sdp_media_add_attr(m, attr);
	    }
	}
    }

    return PJ_SUCCESS;
}

/* Create SDP for call hold. */
static pj_status_t create_sdp_of_call_hold(pjsua_call *call,
					   pjmedia_sdp_session **p_sdp)
{
    pj_status_t status;
    pj_pool_t *pool;
    pjmedia_sdp_session *sdp;

    /* Use call's provisional pool */
    pool = call->inv->pool_prov;

    /* Create new offer */
    status = pjsua_media_channel_create_sdp(call->index, pool, NULL, &sdp,
					    NULL);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create local SDP", status);
	return status;
    }

    status = modify_sdp_of_call_hold(call, pool, sdp);
    if (status != PJ_SUCCESS)
	return status;

    *p_sdp = sdp;

    return PJ_SUCCESS;
}

/*
 * Called when session received new offer.
 */
static void pjsua_call_on_rx_offer(pjsip_inv_session *inv,
				   const pjmedia_sdp_session *offer)
{
    pjsua_call *call;
    pjmedia_sdp_session *answer;
    unsigned i;
    pj_status_t status;

    call = (pjsua_call*) inv->dlg->mod_data[pjsua_var.mod.id];

    /* Supply candidate answer */
    PJ_LOG(4,(THIS_FILE, "Call %d: received updated media offer",
	      call->index));
    pj_log_push_indent();

    if (pjsua_var.ua_cfg.cb.on_call_rx_offer) {
	pjsip_status_code code = PJSIP_SC_OK;
	pjsua_call_setting opt = call->opt;
	
	(*pjsua_var.ua_cfg.cb.on_call_rx_offer)(call->index, offer, NULL,
						&code, &opt);

	if (code != PJSIP_SC_OK) {
	    PJ_LOG(4,(THIS_FILE, "Rejecting updated media offer on call %d",
		      call->index));
	    goto on_return;
	}

	call->opt = opt;
    }
    
    /* Re-init media for the new remote offer before creating SDP */
    status = apply_call_setting(call, &call->opt, offer);
    if (status != PJ_SUCCESS)
	goto on_return;

    status = pjsua_media_channel_create_sdp(call->index, 
					    call->inv->pool_prov, 
					    offer, &answer, NULL);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create local SDP", status);
	goto on_return;
    }

    /* Validate media count in the generated answer */
    pj_assert(answer->media_count == offer->media_count);

    /* Check if offer's conn address is zero */
    for (i = 0; i < answer->media_count; ++i) {
	pjmedia_sdp_conn *conn;

	conn = offer->media[i]->conn;
	if (!conn)
	    conn = offer->conn;

	if (pj_strcmp2(&conn->addr, "0.0.0.0")==0 ||
	    pj_strcmp2(&conn->addr, "0")==0)
	{
	    pjmedia_sdp_conn *a_conn = answer->media[i]->conn;

	    /* Modify answer address */
	    if (a_conn) {
		a_conn->addr = pj_str("0.0.0.0");
	    } else if (answer->conn == NULL ||
		       pj_strcmp2(&answer->conn->addr, "0.0.0.0") != 0)
	    {
		a_conn = PJ_POOL_ZALLOC_T(call->inv->pool_prov,
					  pjmedia_sdp_conn);
		a_conn->net_type = pj_str("IN");
		a_conn->addr_type = pj_str("IP4");
		a_conn->addr = pj_str("0.0.0.0");
		answer->media[i]->conn = a_conn;
	    }
	}
    }

    /* Check if call is on-hold */
    if (call->local_hold) {
	modify_sdp_of_call_hold(call, call->inv->pool_prov, answer);
    }

    status = pjsip_inv_set_sdp_answer(call->inv, answer);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to set answer", status);
	goto on_return;
    }

on_return:
    pj_log_pop_indent();
}


/*
 * Called to generate new offer.
 */
static void pjsua_call_on_create_offer(pjsip_inv_session *inv,
				       pjmedia_sdp_session **offer)
{
    pjsua_call *call;
    pj_status_t status;

    pj_log_push_indent();

    call = (pjsua_call*) inv->dlg->mod_data[pjsua_var.mod.id];

    /* See if we've put call on hold. */
    if (call->local_hold) {
	PJ_LOG(4,(THIS_FILE, 
		  "Call %d: call is on-hold locally, creating call-hold SDP ",
		  call->index));
	status = create_sdp_of_call_hold( call, offer );
    } else {
	PJ_LOG(4,(THIS_FILE, "Call %d: asked to send a new offer",
		  call->index));

	status = pjsua_media_channel_create_sdp(call->index, 
						call->inv->pool_prov, 
					        NULL, offer, NULL);
    }

    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create local SDP", status);
	goto on_return;
    }

on_return:
    pj_log_pop_indent();
}


/*
 * Callback called by event framework when the xfer subscription state
 * has changed.
 */
static void xfer_client_on_evsub_state( pjsip_evsub *sub, pjsip_event *event)
{
    
    PJ_UNUSED_ARG(event);

    pj_log_push_indent();

    /*
     * When subscription is accepted (got 200/OK to REFER), check if 
     * subscription suppressed.
     */
    if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_ACCEPTED) {

	pjsip_rx_data *rdata;
	pjsip_generic_string_hdr *refer_sub;
	const pj_str_t REFER_SUB = { "Refer-Sub", 9 };
	pjsua_call *call;

	call = (pjsua_call*) pjsip_evsub_get_mod_data(sub, pjsua_var.mod.id);

	/* Must be receipt of response message */
	pj_assert(event->type == PJSIP_EVENT_TSX_STATE && 
		  event->body.tsx_state.type == PJSIP_EVENT_RX_MSG);
	rdata = event->body.tsx_state.src.rdata;

	/* Find Refer-Sub header */
	refer_sub = (pjsip_generic_string_hdr*)
		    pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, 
					       &REFER_SUB, NULL);

	/* Check if subscription is suppressed */
	if (refer_sub && pj_stricmp2(&refer_sub->hvalue, "false")==0) {
	    /* Since no subscription is desired, assume that call has been
	     * transfered successfully.
	     */
	    if (call && pjsua_var.ua_cfg.cb.on_call_transfer_status) {
		const pj_str_t ACCEPTED = { "Accepted", 8 };
		pj_bool_t cont = PJ_FALSE;
		(*pjsua_var.ua_cfg.cb.on_call_transfer_status)(call->index, 
							       200,
							       &ACCEPTED,
							       PJ_TRUE,
							       &cont);
	    }

	    /* Yes, subscription is suppressed.
	     * Terminate our subscription now.
	     */
	    PJ_LOG(4,(THIS_FILE, "Xfer subscription suppressed, terminating "
				 "event subcription..."));
	    pjsip_evsub_terminate(sub, PJ_TRUE);

	} else {
	    /* Notify application about call transfer progress. 
	     * Initially notify with 100/Accepted status.
	     */
	    if (call && pjsua_var.ua_cfg.cb.on_call_transfer_status) {
		const pj_str_t ACCEPTED = { "Accepted", 8 };
		pj_bool_t cont = PJ_FALSE;
		(*pjsua_var.ua_cfg.cb.on_call_transfer_status)(call->index, 
							       100,
							       &ACCEPTED,
							       PJ_FALSE,
							       &cont);
	    }
	}
    }
    /*
     * On incoming NOTIFY, notify application about call transfer progress.
     */
    else if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_ACTIVE ||
	     pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) 
    {
	pjsua_call *call;
	pjsip_msg *msg;
	pjsip_msg_body *body;
	pjsip_status_line status_line;
	pj_bool_t is_last;
	pj_bool_t cont;
	pj_status_t status;

	call = (pjsua_call*) pjsip_evsub_get_mod_data(sub, pjsua_var.mod.id);

	/* When subscription is terminated, clear the xfer_sub member of 
	 * the inv_data.
	 */
	if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
	    pjsip_evsub_set_mod_data(sub, pjsua_var.mod.id, NULL);
	    PJ_LOG(4,(THIS_FILE, "Xfer client subscription terminated"));

	}

	if (!call || !event || !pjsua_var.ua_cfg.cb.on_call_transfer_status) {
	    /* Application is not interested with call progress status */
	    goto on_return;
	}

	/* This better be a NOTIFY request */
	if (event->type == PJSIP_EVENT_TSX_STATE &&
	    event->body.tsx_state.type == PJSIP_EVENT_RX_MSG)
	{
	    pjsip_rx_data *rdata;

	    rdata = event->body.tsx_state.src.rdata;

	    /* Check if there's body */
	    msg = rdata->msg_info.msg;
	    body = msg->body;
	    if (!body) {
		PJ_LOG(2,(THIS_FILE, 
			  "Warning: received NOTIFY without message body"));
		goto on_return;
	    }

	    /* Check for appropriate content */
	    if (pj_stricmp2(&body->content_type.type, "message") != 0 ||
		pj_stricmp2(&body->content_type.subtype, "sipfrag") != 0)
	    {
		PJ_LOG(2,(THIS_FILE, 
			  "Warning: received NOTIFY with non message/sipfrag "
			  "content"));
		goto on_return;
	    }

	    /* Try to parse the content */
	    status = pjsip_parse_status_line((char*)body->data, body->len, 
					     &status_line);
	    if (status != PJ_SUCCESS) {
		PJ_LOG(2,(THIS_FILE, 
			  "Warning: received NOTIFY with invalid "
			  "message/sipfrag content"));
		goto on_return;
	    }

	} else {
	    status_line.code = 500;
	    status_line.reason = *pjsip_get_status_text(500);
	}

	/* Notify application */
	is_last = (pjsip_evsub_get_state(sub)==PJSIP_EVSUB_STATE_TERMINATED);
	cont = !is_last;
	(*pjsua_var.ua_cfg.cb.on_call_transfer_status)(call->index, 
						       status_line.code,
						       &status_line.reason,
						       is_last, &cont);

	if (!cont) {
	    pjsip_evsub_set_mod_data(sub, pjsua_var.mod.id, NULL);
	}

	/* If the call transfer has completed but the subscription is
	 * not terminated, terminate it now.
	 */
	if (status_line.code/100 == 2 && !is_last) {
	    pjsip_tx_data *tdata;

	    status = pjsip_evsub_initiate(sub, &pjsip_subscribe_method, 
					  0, &tdata);
	    if (status == PJ_SUCCESS)
		status = pjsip_evsub_send_request(sub, tdata);
	}
    }

on_return:
    pj_log_pop_indent();
}


/*
 * Callback called by event framework when the xfer subscription state
 * has changed.
 */
static void xfer_server_on_evsub_state( pjsip_evsub *sub, pjsip_event *event)
{
    PJ_UNUSED_ARG(event);

    pj_log_push_indent();

    /*
     * When subscription is terminated, clear the xfer_sub member of 
     * the inv_data.
     */
    if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
	pjsua_call *call;

	call = (pjsua_call*) pjsip_evsub_get_mod_data(sub, pjsua_var.mod.id);
	if (!call)
	    goto on_return;

	pjsip_evsub_set_mod_data(sub, pjsua_var.mod.id, NULL);
	call->xfer_sub = NULL;

	PJ_LOG(4,(THIS_FILE, "Xfer server subscription terminated"));
    }

on_return:
    pj_log_pop_indent();
}


/*
 * Follow transfer (REFER) request.
 */
static void on_call_transfered( pjsip_inv_session *inv,
			        pjsip_rx_data *rdata )
{
    pj_status_t status;
    pjsip_tx_data *tdata;
    pjsua_call *existing_call;
    int new_call;
    const pj_str_t str_refer_to = { "Refer-To", 8};
    const pj_str_t str_refer_sub = { "Refer-Sub", 9 };
    const pj_str_t str_ref_by = { "Referred-By", 11 };
    pjsip_generic_string_hdr *refer_to;
    pjsip_generic_string_hdr *refer_sub;
    pjsip_hdr *ref_by_hdr;
    pj_bool_t no_refer_sub = PJ_FALSE;
    char *uri;
    pjsua_msg_data msg_data;
    pj_str_t tmp;
    pjsip_status_code code;
    pjsip_evsub *sub;
    pjsua_call_setting call_opt;

    pj_log_push_indent();

    existing_call = (pjsua_call*) inv->dlg->mod_data[pjsua_var.mod.id];

    /* Find the Refer-To header */
    refer_to = (pjsip_generic_string_hdr*)
	pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_refer_to, NULL);

    if (refer_to == NULL) {
	/* Invalid Request.
	 * No Refer-To header!
	 */
	PJ_LOG(4,(THIS_FILE, "Received REFER without Refer-To header!"));
	pjsip_dlg_respond( inv->dlg, rdata, 400, NULL, NULL, NULL);
	goto on_return;
    }

    /* Find optional Refer-Sub header */
    refer_sub = (pjsip_generic_string_hdr*)
	pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_refer_sub, NULL);

    if (refer_sub) {
	if (!pj_strnicmp2(&refer_sub->hvalue, "true", 4)==0)
	    no_refer_sub = PJ_TRUE;
    }

    /* Find optional Referred-By header (to be copied onto outgoing INVITE
     * request.
     */
    ref_by_hdr = (pjsip_hdr*)
		 pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_ref_by, 
					    NULL);

    /* Notify callback */
    code = PJSIP_SC_ACCEPTED;
    if (pjsua_var.ua_cfg.cb.on_call_transfer_request) {
	(*pjsua_var.ua_cfg.cb.on_call_transfer_request)(existing_call->index,
							&refer_to->hvalue, 
							&code);
    }

    call_opt = existing_call->opt;
    if (pjsua_var.ua_cfg.cb.on_call_transfer_request2) {
	(*pjsua_var.ua_cfg.cb.on_call_transfer_request2)(existing_call->index,
							 &refer_to->hvalue, 
							 &code,
							 &call_opt);
    }

    if (code < 200)
	code = PJSIP_SC_ACCEPTED;
    if (code >= 300) {
	/* Application rejects call transfer request */
	pjsip_dlg_respond( inv->dlg, rdata, code, NULL, NULL, NULL);
	goto on_return;
    }

    PJ_LOG(3,(THIS_FILE, "Call to %.*s is being transfered to %.*s",
	      (int)inv->dlg->remote.info_str.slen,
	      inv->dlg->remote.info_str.ptr,
	      (int)refer_to->hvalue.slen, 
	      refer_to->hvalue.ptr));

    if (no_refer_sub) {
	/*
	 * Always answer with 2xx.
	 */
	pjsip_tx_data *tdata;
	const pj_str_t str_false = { "false", 5};
	pjsip_hdr *hdr;

	status = pjsip_dlg_create_response(inv->dlg, rdata, code, NULL, 
					   &tdata);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to create 2xx response to REFER",
			 status);
	    goto on_return;
	}

	/* Add Refer-Sub header */
	hdr = (pjsip_hdr*) 
	       pjsip_generic_string_hdr_create(tdata->pool, &str_refer_sub,
					      &str_false);
	pjsip_msg_add_hdr(tdata->msg, hdr);


	/* Send answer */
	status = pjsip_dlg_send_response(inv->dlg, pjsip_rdata_get_tsx(rdata),
					 tdata);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to create 2xx response to REFER",
			 status);
	    goto on_return;
	}

	/* Don't have subscription */
	sub = NULL;

    } else {
	struct pjsip_evsub_user xfer_cb;
	pjsip_hdr hdr_list;

	/* Init callback */
	pj_bzero(&xfer_cb, sizeof(xfer_cb));
	xfer_cb.on_evsub_state = &xfer_server_on_evsub_state;

	/* Init additional header list to be sent with REFER response */
	pj_list_init(&hdr_list);

	/* Create transferee event subscription */
	status = pjsip_xfer_create_uas( inv->dlg, &xfer_cb, rdata, &sub);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to create xfer uas", status);
	    pjsip_dlg_respond( inv->dlg, rdata, 500, NULL, NULL, NULL);
	    goto on_return;
	}

	/* If there's Refer-Sub header and the value is "true", send back
	 * Refer-Sub in the response with value "true" too.
	 */
	if (refer_sub) {
	    const pj_str_t str_true = { "true", 4 };
	    pjsip_hdr *hdr;

	    hdr = (pjsip_hdr*) 
		   pjsip_generic_string_hdr_create(inv->dlg->pool, 
						   &str_refer_sub,
						   &str_true);
	    pj_list_push_back(&hdr_list, hdr);

	}

	/* Accept the REFER request, send 2xx. */
	pjsip_xfer_accept(sub, rdata, code, &hdr_list);

	/* Create initial NOTIFY request */
	status = pjsip_xfer_notify( sub, PJSIP_EVSUB_STATE_ACTIVE,
				    100, NULL, &tdata);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to create NOTIFY to REFER", 
			 status);
	    goto on_return;
	}

	/* Send initial NOTIFY request */
	status = pjsip_xfer_send_request( sub, tdata);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to send NOTIFY to REFER", status);
	    goto on_return;
	}
    }

    /* We're cheating here.
     * We need to get a null terminated string from a pj_str_t.
     * So grab the pointer from the hvalue and NULL terminate it, knowing
     * that the NULL position will be occupied by a newline. 
     */
    uri = refer_to->hvalue.ptr;
    uri[refer_to->hvalue.slen] = '\0';

    /* Init msg_data */
    pjsua_msg_data_init(&msg_data);

    /* If Referred-By header is present in the REFER request, copy this
     * to the outgoing INVITE request.
     */
    if (ref_by_hdr != NULL) {
	pjsip_hdr *dup = (pjsip_hdr*)
			 pjsip_hdr_clone(rdata->tp_info.pool, ref_by_hdr);
	pj_list_push_back(&msg_data.hdr_list, dup);
    }

    /* Now make the outgoing call. */
    tmp = pj_str(uri);
    status = pjsua_call_make_call(existing_call->acc_id, &tmp, &call_opt,
				  existing_call->user_data, &msg_data, 
				  &new_call);
    if (status != PJ_SUCCESS) {

	/* Notify xferer about the error (if we have subscription) */
	if (sub) {
	    status = pjsip_xfer_notify(sub, PJSIP_EVSUB_STATE_TERMINATED,
				       500, NULL, &tdata);
	    if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "Unable to create NOTIFY to REFER", 
			      status);
		goto on_return;
	    }
	    status = pjsip_xfer_send_request(sub, tdata);
	    if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "Unable to send NOTIFY to REFER", 
			      status);
		goto on_return;
	    }
	}
	goto on_return;
    }

    if (sub) {
	/* Put the server subscription in inv_data.
	 * Subsequent state changed in pjsua_inv_on_state_changed() will be
	 * reported back to the server subscription.
	 */
	pjsua_var.calls[new_call].xfer_sub = sub;

	/* Put the invite_data in the subscription. */
	pjsip_evsub_set_mod_data(sub, pjsua_var.mod.id, 
				 &pjsua_var.calls[new_call]);
    }

on_return:
    pj_log_pop_indent();
}



/*
 * This callback is called when transaction state has changed in INVITE
 * session. We use this to trap:
 *  - incoming REFER request.
 *  - incoming MESSAGE request.
 */
static void pjsua_call_on_tsx_state_changed(pjsip_inv_session *inv,
					    pjsip_transaction *tsx,
					    pjsip_event *e)
{
    pjsua_call *call;

    pj_log_push_indent();

    call = (pjsua_call*) inv->dlg->mod_data[pjsua_var.mod.id];

    if (call == NULL)
	goto on_return;

    if (call->inv == NULL) {
	/* Shouldn't happen. It happens only when we don't terminate the
	 * server subscription caused by REFER after the call has been
	 * transfered (and this call has been disconnected), and we
	 * receive another REFER for this call.
	 */
	goto on_return;
    }

    /* https://trac.pjsip.org/repos/ticket/1452:
     *    If a request is retried due to 401/407 challenge, don't process the
     *    transaction first but wait until we've retried it.
     */
    if (tsx->role == PJSIP_ROLE_UAC &&
	(tsx->status_code==401 || tsx->status_code==407) &&
	tsx->last_tx && tsx->last_tx->auth_retry)
    {
	goto on_return;
    }

    /* Notify application callback first */
    if (pjsua_var.ua_cfg.cb.on_call_tsx_state) {
	(*pjsua_var.ua_cfg.cb.on_call_tsx_state)(call->index, tsx, e);
    }

    if (tsx->role==PJSIP_ROLE_UAS &&
	tsx->state==PJSIP_TSX_STATE_TRYING &&
	pjsip_method_cmp(&tsx->method, pjsip_get_refer_method())==0)
    {
	/*
	 * Incoming REFER request.
	 */
	on_call_transfered(call->inv, e->body.tsx_state.src.rdata);

    }
    else if (tsx->role==PJSIP_ROLE_UAS &&
	     tsx->state==PJSIP_TSX_STATE_TRYING &&
	     pjsip_method_cmp(&tsx->method, &pjsip_message_method)==0)
    {
	/*
	 * Incoming MESSAGE request!
	 */
	pjsip_rx_data *rdata;
	pjsip_msg *msg;
	pjsip_accept_hdr *accept_hdr;
	pj_status_t status;

	rdata = e->body.tsx_state.src.rdata;
	msg = rdata->msg_info.msg;

	/* Request MUST have message body, with Content-Type equal to
	 * "text/plain".
	 */
	if (pjsua_im_accept_pager(rdata, &accept_hdr) == PJ_FALSE) {

	    pjsip_hdr hdr_list;

	    pj_list_init(&hdr_list);
	    pj_list_push_back(&hdr_list, accept_hdr);

	    pjsip_dlg_respond( inv->dlg, rdata, PJSIP_SC_NOT_ACCEPTABLE_HERE, 
			       NULL, &hdr_list, NULL );
	    goto on_return;
	}

	/* Respond with 200 first, so that remote doesn't retransmit in case
	 * the UI takes too long to process the message. 
	 */
	status = pjsip_dlg_respond( inv->dlg, rdata, 200, NULL, NULL, NULL);

	/* Process MESSAGE request */
	pjsua_im_process_pager(call->index, &inv->dlg->remote.info_str,
			       &inv->dlg->local.info_str, rdata);

    }
    else if (tsx->role == PJSIP_ROLE_UAC &&
	     pjsip_method_cmp(&tsx->method, &pjsip_message_method)==0)
    {
	/* Handle outgoing pager status */
	if (tsx->status_code >= 200) {
	    pjsua_im_data *im_data;

	    im_data = (pjsua_im_data*) tsx->mod_data[pjsua_var.mod.id];
	    /* im_data can be NULL if this is typing indication */

	    if (im_data && pjsua_var.ua_cfg.cb.on_pager_status) {
		pjsua_var.ua_cfg.cb.on_pager_status(im_data->call_id,
						    &im_data->to,
						    &im_data->body,
						    im_data->user_data,
						    (pjsip_status_code)
						    	tsx->status_code,
						    &tsx->status_text);
	    }
	}
    } else if (tsx->role == PJSIP_ROLE_UAC &&
	       tsx->last_tx == (pjsip_tx_data*)call->hold_msg &&
	       tsx->state >= PJSIP_TSX_STATE_COMPLETED)
    {
	/* Monitor the status of call hold request */
	call->hold_msg = NULL;
	if (tsx->status_code/100 != 2) {
	    /* Outgoing call hold failed */
	    call->local_hold = PJ_FALSE;
	    PJ_LOG(3,(THIS_FILE, "Error putting call %d on hold (reason=%d)",
		      call->index, tsx->status_code));
	}
    } else if (tsx->role==PJSIP_ROLE_UAS &&
	tsx->state==PJSIP_TSX_STATE_TRYING &&
	pjsip_method_cmp(&tsx->method, &pjsip_info_method)==0)
    {
	/*
	 * Incoming INFO request for media control.
	 */
	const pj_str_t STR_APPLICATION	     = { "application", 11};
	const pj_str_t STR_MEDIA_CONTROL_XML = { "media_control+xml", 17 };
	pjsip_rx_data *rdata = e->body.tsx_state.src.rdata;
	pjsip_msg_body *body = rdata->msg_info.msg->body;

	if (body && body->len &&
	    pj_stricmp(&body->content_type.type, &STR_APPLICATION)==0 &&
	    pj_stricmp(&body->content_type.subtype, &STR_MEDIA_CONTROL_XML)==0)
	{
	    pjsip_tx_data *tdata;
	    pj_str_t control_st;
	    pj_status_t status;

	    /* Apply and answer the INFO request */
	    pj_strset(&control_st, (char*)body->data, body->len);
	    status = pjsua_media_apply_xml_control(call->index, &control_st);
	    if (status == PJ_SUCCESS) {
		status = pjsip_endpt_create_response(tsx->endpt, rdata,
						     200, NULL, &tdata);
		if (status == PJ_SUCCESS)
		    status = pjsip_tsx_send_msg(tsx, tdata);
	    } else {
		status = pjsip_endpt_create_response(tsx->endpt, rdata,
						     400, NULL, &tdata);
		if (status == PJ_SUCCESS)
		    status = pjsip_tsx_send_msg(tsx, tdata);
	    }
	}
    }

on_return:
    pj_log_pop_indent();
}


/* Redirection handler */
static pjsip_redirect_op pjsua_call_on_redirected(pjsip_inv_session *inv,
						  const pjsip_uri *target,
						  const pjsip_event *e)
{
    pjsua_call *call = (pjsua_call*) inv->dlg->mod_data[pjsua_var.mod.id];
    pjsip_redirect_op op;

    pj_log_push_indent();

    if (pjsua_var.ua_cfg.cb.on_call_redirected) {
	op = (*pjsua_var.ua_cfg.cb.on_call_redirected)(call->index, 
							 target, e);
    } else {
	PJ_LOG(4,(THIS_FILE, "Unhandled redirection for call %d "
		  "(callback not implemented by application). Disconnecting "
		  "call.",
		  call->index));
	op = PJSIP_REDIRECT_STOP;
    }

    pj_log_pop_indent();

    return op;
}

