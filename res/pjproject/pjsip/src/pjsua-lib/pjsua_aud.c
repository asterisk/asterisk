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

#if defined(PJSUA_MEDIA_HAS_PJMEDIA) && PJSUA_MEDIA_HAS_PJMEDIA != 0

#define THIS_FILE		"pjsua_aud.c"
#define NULL_SND_DEV_ID		-99

/*****************************************************************************
 *
 * Prototypes
 */
/* Open sound dev */
static pj_status_t open_snd_dev(pjmedia_snd_port_param *param);
/* Close existing sound device */
static void close_snd_dev(void);
/* Create audio device param */
static pj_status_t create_aud_param(pjmedia_aud_param *param,
				    pjmedia_aud_dev_index capture_dev,
				    pjmedia_aud_dev_index playback_dev,
				    unsigned clock_rate,
				    unsigned channel_count,
				    unsigned samples_per_frame,
				    unsigned bits_per_sample);

/*****************************************************************************
 *
 * Call API that are closely tied to PJMEDIA
 */
/*
 * Check if call has an active media session.
 */
PJ_DEF(pj_bool_t) pjsua_call_has_media(pjsua_call_id call_id)
{
    pjsua_call *call = &pjsua_var.calls[call_id];
    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);
    return call->audio_idx >= 0 && call->media[call->audio_idx].strm.a.stream;
}


/*
 * Get the conference port identification associated with the call.
 */
PJ_DEF(pjsua_conf_port_id) pjsua_call_get_conf_port(pjsua_call_id call_id)
{
    pjsua_call *call;
    pjsua_conf_port_id port_id = PJSUA_INVALID_ID;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    /* Use PJSUA_LOCK() instead of acquire_call():
     *  https://trac.pjsip.org/repos/ticket/1371
     */
    PJSUA_LOCK();

    if (!pjsua_call_is_active(call_id))
	goto on_return;

    call = &pjsua_var.calls[call_id];
    port_id = call->media[call->audio_idx].strm.a.conf_slot;

on_return:
    PJSUA_UNLOCK();

    return port_id;
}


/*
 * Get media stream info for the specified media index.
 */
PJ_DEF(pj_status_t) pjsua_call_get_stream_info( pjsua_call_id call_id,
                                                unsigned med_idx,
                                                pjsua_stream_info *psi)
{
    pjsua_call *call;
    pjsua_call_media *call_med;
    pj_status_t status;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(psi, PJ_EINVAL);

    PJSUA_LOCK();

    call = &pjsua_var.calls[call_id];

    if (med_idx >= call->med_cnt) {
	PJSUA_UNLOCK();
	return PJ_EINVAL;
    }

    call_med = &call->media[med_idx];
    psi->type = call_med->type;
    switch (call_med->type) {
    case PJMEDIA_TYPE_AUDIO:
	status = pjmedia_stream_get_info(call_med->strm.a.stream,
					 &psi->info.aud);
	break;
#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)
    case PJMEDIA_TYPE_VIDEO:
	status = pjmedia_vid_stream_get_info(call_med->strm.v.stream,
					     &psi->info.vid);
	break;
#endif
    default:
	status = PJMEDIA_EINVALIMEDIATYPE;
	break;
    }

    PJSUA_UNLOCK();
    return status;
}


/*
 *  Get media stream statistic for the specified media index.
 */
PJ_DEF(pj_status_t) pjsua_call_get_stream_stat( pjsua_call_id call_id,
                                                unsigned med_idx,
                                                pjsua_stream_stat *stat)
{
    pjsua_call *call;
    pjsua_call_media *call_med;
    pj_status_t status;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(stat, PJ_EINVAL);

    PJSUA_LOCK();

    call = &pjsua_var.calls[call_id];

    if (med_idx >= call->med_cnt) {
	PJSUA_UNLOCK();
	return PJ_EINVAL;
    }

    call_med = &call->media[med_idx];
    switch (call_med->type) {
    case PJMEDIA_TYPE_AUDIO:
	status = pjmedia_stream_get_stat(call_med->strm.a.stream,
					 &stat->rtcp);
	if (status == PJ_SUCCESS)
	    status = pjmedia_stream_get_stat_jbuf(call_med->strm.a.stream,
						  &stat->jbuf);
	break;
#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)
    case PJMEDIA_TYPE_VIDEO:
	status = pjmedia_vid_stream_get_stat(call_med->strm.v.stream,
					     &stat->rtcp);
	if (status == PJ_SUCCESS)
	    status = pjmedia_vid_stream_get_stat_jbuf(call_med->strm.v.stream,
						  &stat->jbuf);
	break;
#endif
    default:
	status = PJMEDIA_EINVALIMEDIATYPE;
	break;
    }

    PJSUA_UNLOCK();
    return status;
}

/*
 * Send DTMF digits to remote using RFC 2833 payload formats.
 */
PJ_DEF(pj_status_t) pjsua_call_dial_dtmf( pjsua_call_id call_id,
					  const pj_str_t *digits)
{
    pjsua_call *call;
    pjsip_dialog *dlg = NULL;
    pj_status_t status;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Call %d dialing DTMF %.*s",
    			 call_id, (int)digits->slen, digits->ptr));
    pj_log_push_indent();

    status = acquire_call("pjsua_call_dial_dtmf()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	goto on_return;

    if (!pjsua_call_has_media(call_id)) {
	PJ_LOG(3,(THIS_FILE, "Media is not established yet!"));
	status = PJ_EINVALIDOP;
	goto on_return;
    }

    status = pjmedia_stream_dial_dtmf(
		call->media[call->audio_idx].strm.a.stream, digits);

on_return:
    if (dlg) pjsip_dlg_dec_lock(dlg);
    pj_log_pop_indent();
    return status;
}


/*****************************************************************************
 *
 * Audio media with PJMEDIA backend
 */

/* Init pjmedia audio subsystem */
pj_status_t pjsua_aud_subsys_init()
{
    pj_str_t codec_id = {NULL, 0};
    unsigned opt;
    pjmedia_audio_codec_config codec_cfg;
    pj_status_t status;

    /* To suppress warning about unused var when all codecs are disabled */
    PJ_UNUSED_ARG(codec_id);

    /*
     * Register all codecs
     */
    pjmedia_audio_codec_config_default(&codec_cfg);
    codec_cfg.speex.quality = pjsua_var.media_cfg.quality;
    codec_cfg.speex.complexity = -1;
    codec_cfg.ilbc.mode = pjsua_var.media_cfg.ilbc_mode;

#if PJMEDIA_HAS_PASSTHROUGH_CODECS
    /* Register passthrough codecs */
    {
	unsigned aud_idx;
	unsigned ext_fmt_cnt = 0;
	pjmedia_format ext_fmts[32];

	/* List extended formats supported by audio devices */
	for (aud_idx = 0; aud_idx < pjmedia_aud_dev_count(); ++aud_idx) {
	    pjmedia_aud_dev_info aud_info;
	    unsigned i;

	    status = pjmedia_aud_dev_get_info(aud_idx, &aud_info);
	    if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "Error querying audio device info",
			     status);
		goto on_error;
	    }

	    /* Collect extended formats supported by this audio device */
	    for (i = 0; i < aud_info.ext_fmt_cnt; ++i) {
		unsigned j;
		pj_bool_t is_listed = PJ_FALSE;

		/* See if this extended format is already in the list */
		for (j = 0; j < ext_fmt_cnt && !is_listed; ++j) {
		    if (ext_fmts[j].id == aud_info.ext_fmt[i].id &&
			ext_fmts[j].det.aud.avg_bps ==
			aud_info.ext_fmt[i].det.aud.avg_bps)
		    {
			is_listed = PJ_TRUE;
		    }
		}

		/* Put this format into the list, if it is not in the list */
		if (!is_listed)
		    ext_fmts[ext_fmt_cnt++] = aud_info.ext_fmt[i];

		pj_assert(ext_fmt_cnt <= PJ_ARRAY_SIZE(ext_fmts));
	    }
	}

	/* Init the passthrough codec with supported formats only */
	codec_cfg.passthrough.setting.fmt_cnt = ext_fmt_cnt;
	codec_cfg.passthrough.setting.fmts = ext_fmts;
	codec_cfg.passthrough.setting.ilbc_mode = cfg->ilbc_mode;
    }
#endif /* PJMEDIA_HAS_PASSTHROUGH_CODECS */

    /* Register all codecs */
    status = pjmedia_codec_register_audio_codecs(pjsua_var.med_endpt,
                                                 &codec_cfg);
    if (status != PJ_SUCCESS) {
	PJ_PERROR(1,(THIS_FILE, status, "Error registering codecs"));
	goto on_error;
    }

    /* Set speex/16000 to higher priority*/
    codec_id = pj_str("speex/16000");
    pjmedia_codec_mgr_set_codec_priority(
	pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt),
	&codec_id, PJMEDIA_CODEC_PRIO_NORMAL+2);

    /* Set speex/8000 to next higher priority*/
    codec_id = pj_str("speex/8000");
    pjmedia_codec_mgr_set_codec_priority(
	pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt),
	&codec_id, PJMEDIA_CODEC_PRIO_NORMAL+1);

    /* Disable ALL L16 codecs */
    codec_id = pj_str("L16");
    pjmedia_codec_mgr_set_codec_priority(
	pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt),
	&codec_id, PJMEDIA_CODEC_PRIO_DISABLED);


    /* Save additional conference bridge parameters for future
     * reference.
     */
    pjsua_var.mconf_cfg.channel_count = pjsua_var.media_cfg.channel_count;
    pjsua_var.mconf_cfg.bits_per_sample = 16;
    pjsua_var.mconf_cfg.samples_per_frame = pjsua_var.media_cfg.clock_rate *
					    pjsua_var.mconf_cfg.channel_count *
					    pjsua_var.media_cfg.audio_frame_ptime /
					    1000;

    /* Init options for conference bridge. */
    opt = PJMEDIA_CONF_NO_DEVICE;
    if (pjsua_var.media_cfg.quality >= 3 &&
	pjsua_var.media_cfg.quality <= 4)
    {
	opt |= PJMEDIA_CONF_SMALL_FILTER;
    }
    else if (pjsua_var.media_cfg.quality < 3) {
	opt |= PJMEDIA_CONF_USE_LINEAR;
    }

    /* Init conference bridge. */
    status = pjmedia_conf_create(pjsua_var.pool,
				 pjsua_var.media_cfg.max_media_ports,
				 pjsua_var.media_cfg.clock_rate,
				 pjsua_var.mconf_cfg.channel_count,
				 pjsua_var.mconf_cfg.samples_per_frame,
				 pjsua_var.mconf_cfg.bits_per_sample,
				 opt, &pjsua_var.mconf);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error creating conference bridge",
		     status);
	goto on_error;
    }

    /* Are we using the audio switchboard (a.k.a APS-Direct)? */
    pjsua_var.is_mswitch = pjmedia_conf_get_master_port(pjsua_var.mconf)
			    ->info.signature == PJMEDIA_CONF_SWITCH_SIGNATURE;

    /* Create null port just in case user wants to use null sound. */
    status = pjmedia_null_port_create(pjsua_var.pool,
				      pjsua_var.media_cfg.clock_rate,
				      pjsua_var.mconf_cfg.channel_count,
				      pjsua_var.mconf_cfg.samples_per_frame,
				      pjsua_var.mconf_cfg.bits_per_sample,
				      &pjsua_var.null_port);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

    return status;

on_error:
    return status;
}

/* Check if sound device is idle. */
static void check_snd_dev_idle()
{
    unsigned call_cnt;

    /* Check if the sound device auto-close feature is disabled. */
    if (pjsua_var.media_cfg.snd_auto_close_time < 0)
	return;

    /* Check if the sound device is currently closed. */
    if (!pjsua_var.snd_is_on)
	return;

    /* Get the call count, we shouldn't close the sound device when there is
     * any calls active.
     */
    call_cnt = pjsua_call_get_count();

    /* When this function is called from pjsua_media_channel_deinit() upon
     * disconnecting call, actually the call count hasn't been updated/
     * decreased. So we put additional check here, if there is only one
     * call and it's in DISCONNECTED state, there is actually no active
     * call.
     */
    if (call_cnt == 1) {
	pjsua_call_id call_id;
	pj_status_t status;

	status = pjsua_enum_calls(&call_id, &call_cnt);
	if (status == PJ_SUCCESS && call_cnt > 0 &&
	    !pjsua_call_is_active(call_id))
	{
	    call_cnt = 0;
	}
    }

    /* Activate sound device auto-close timer if sound device is idle.
     * It is idle when there is no port connection in the bridge and
     * there is no active call.
     */
    if (pjsua_var.snd_idle_timer.id == PJ_FALSE &&
	call_cnt == 0 &&
	pjmedia_conf_get_connect_count(pjsua_var.mconf) == 0)
    {
	pj_time_val delay;

	delay.msec = 0;
	delay.sec = pjsua_var.media_cfg.snd_auto_close_time;

	pjsua_var.snd_idle_timer.id = PJ_TRUE;
	pjsip_endpt_schedule_timer(pjsua_var.endpt, &pjsua_var.snd_idle_timer,
				   &delay);
    }
}

/* Timer callback to close sound device */
static void close_snd_timer_cb( pj_timer_heap_t *th,
				pj_timer_entry *entry)
{
    PJ_UNUSED_ARG(th);

    PJSUA_LOCK();
    if (entry->id) {
	PJ_LOG(4,(THIS_FILE,"Closing sound device after idle for %d seconds",
		  pjsua_var.media_cfg.snd_auto_close_time));

	entry->id = PJ_FALSE;

	close_snd_dev();
    }
    PJSUA_UNLOCK();
}

pj_status_t pjsua_aud_subsys_start(void)
{
    pj_status_t status = PJ_SUCCESS;

    pj_timer_entry_init(&pjsua_var.snd_idle_timer, PJ_FALSE, NULL,
			&close_snd_timer_cb);

    return status;
}

pj_status_t pjsua_aud_subsys_destroy()
{
    unsigned i;

    close_snd_dev();

    if (pjsua_var.mconf) {
	pjmedia_conf_destroy(pjsua_var.mconf);
	pjsua_var.mconf = NULL;
    }

    if (pjsua_var.null_port) {
	pjmedia_port_destroy(pjsua_var.null_port);
	pjsua_var.null_port = NULL;
    }

    /* Destroy file players */
    for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.player); ++i) {
	if (pjsua_var.player[i].port) {
	    pjmedia_port_destroy(pjsua_var.player[i].port);
	    pjsua_var.player[i].port = NULL;
	}
    }

    /* Destroy file recorders */
    for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.recorder); ++i) {
	if (pjsua_var.recorder[i].port) {
	    pjmedia_port_destroy(pjsua_var.recorder[i].port);
	    pjsua_var.recorder[i].port = NULL;
	}
    }

    return PJ_SUCCESS;
}

void pjsua_aud_stop_stream(pjsua_call_media *call_med)
{
    pjmedia_stream *strm = call_med->strm.a.stream;
    pjmedia_rtcp_stat stat;

    if (strm) {
	pjmedia_stream_send_rtcp_bye(strm);

	if (call_med->strm.a.conf_slot != PJSUA_INVALID_ID) {
	    if (pjsua_var.mconf) {
		pjsua_conf_remove_port(call_med->strm.a.conf_slot);
	    }
	    call_med->strm.a.conf_slot = PJSUA_INVALID_ID;
	}

	if ((call_med->dir & PJMEDIA_DIR_ENCODING) &&
	    (pjmedia_stream_get_stat(strm, &stat) == PJ_SUCCESS))
	{
	    /* Save RTP timestamp & sequence, so when media session is
	     * restarted, those values will be restored as the initial
	     * RTP timestamp & sequence of the new media session. So in
	     * the same call session, RTP timestamp and sequence are
	     * guaranteed to be contigue.
	     */
	    call_med->rtp_tx_seq_ts_set = 1 | (1 << 1);
	    call_med->rtp_tx_seq = stat.rtp_tx_last_seq;
	    call_med->rtp_tx_ts = stat.rtp_tx_last_ts;
	}

	if (pjsua_var.ua_cfg.cb.on_stream_destroyed) {
	    pjsua_var.ua_cfg.cb.on_stream_destroyed(call_med->call->index,
	                                            strm, call_med->idx);
	}

	pjmedia_stream_destroy(strm);
	call_med->strm.a.stream = NULL;
    }

    check_snd_dev_idle();
}

/*
 * DTMF callback from the stream.
 */
static void dtmf_callback(pjmedia_stream *strm, void *user_data,
			  int digit)
{
    PJ_UNUSED_ARG(strm);

    pj_log_push_indent();

    /* For discussions about call mutex protection related to this
     * callback, please see ticket #460:
     *	http://trac.pjsip.org/repos/ticket/460#comment:4
     */
    if (pjsua_var.ua_cfg.cb.on_dtmf_digit) {
	pjsua_call_id call_id;

	call_id = (pjsua_call_id)(long)user_data;
	pjsua_var.ua_cfg.cb.on_dtmf_digit(call_id, digit);
    }

    pj_log_pop_indent();
}


pj_status_t pjsua_aud_channel_update(pjsua_call_media *call_med,
                                     pj_pool_t *tmp_pool,
                                     pjmedia_stream_info *si,
				     const pjmedia_sdp_session *local_sdp,
				     const pjmedia_sdp_session *remote_sdp)
{
    pjsua_call *call = call_med->call;
    pjmedia_port *media_port;
    unsigned strm_idx = call_med->idx;
    pj_status_t status = PJ_SUCCESS;

    PJ_UNUSED_ARG(tmp_pool);
    PJ_UNUSED_ARG(local_sdp);
    PJ_UNUSED_ARG(remote_sdp);

    PJ_LOG(4,(THIS_FILE,"Audio channel update.."));
    pj_log_push_indent();

    si->rtcp_sdes_bye_disabled = PJ_TRUE;

    /* Check if no media is active */
    if (si->dir != PJMEDIA_DIR_NONE) {

	/* Override ptime, if this option is specified. */
	if (pjsua_var.media_cfg.ptime != 0) {
	    si->param->setting.frm_per_pkt = (pj_uint8_t)
		(pjsua_var.media_cfg.ptime / si->param->info.frm_ptime);
	    if (si->param->setting.frm_per_pkt == 0)
		si->param->setting.frm_per_pkt = 1;
	}

	/* Disable VAD, if this option is specified. */
	if (pjsua_var.media_cfg.no_vad) {
	    si->param->setting.vad = 0;
	}


	/* Optionally, application may modify other stream settings here
	 * (such as jitter buffer parameters, codec ptime, etc.)
	 */
	si->jb_init = pjsua_var.media_cfg.jb_init;
	si->jb_min_pre = pjsua_var.media_cfg.jb_min_pre;
	si->jb_max_pre = pjsua_var.media_cfg.jb_max_pre;
	si->jb_max = pjsua_var.media_cfg.jb_max;

	/* Set SSRC */
	si->ssrc = call_med->ssrc;

	/* Set RTP timestamp & sequence, normally these value are intialized
	 * automatically when stream session created, but for some cases (e.g:
	 * call reinvite, call update) timestamp and sequence need to be kept
	 * contigue.
	 */
	si->rtp_ts = call_med->rtp_tx_ts;
	si->rtp_seq = call_med->rtp_tx_seq;
	si->rtp_seq_ts_set = call_med->rtp_tx_seq_ts_set;

#if defined(PJMEDIA_STREAM_ENABLE_KA) && PJMEDIA_STREAM_ENABLE_KA!=0
	/* Enable/disable stream keep-alive and NAT hole punch. */
	si->use_ka = pjsua_var.acc[call->acc_id].cfg.use_stream_ka;
#endif

	/* Create session based on session info. */
	status = pjmedia_stream_create(pjsua_var.med_endpt, NULL, si,
				       call_med->tp, NULL,
				       &call_med->strm.a.stream);
	if (status != PJ_SUCCESS) {
	    goto on_return;
	}

	/* Start stream */
	status = pjmedia_stream_start(call_med->strm.a.stream);
	if (status != PJ_SUCCESS) {
	    goto on_return;
	}

        if (call_med->prev_state == PJSUA_CALL_MEDIA_NONE)
            pjmedia_stream_send_rtcp_sdes(call_med->strm.a.stream);

	/* If DTMF callback is installed by application, install our
	 * callback to the session.
	 */
	if (pjsua_var.ua_cfg.cb.on_dtmf_digit) {
	    pjmedia_stream_set_dtmf_callback(call_med->strm.a.stream,
					     &dtmf_callback,
					     (void*)(long)(call->index));
	}

	/* Get the port interface of the first stream in the session.
	 * We need the port interface to add to the conference bridge.
	 */
	pjmedia_stream_get_port(call_med->strm.a.stream, &media_port);

	/* Notify application about stream creation.
	 * Note: application may modify media_port to point to different
	 * media port
	 */
	if (pjsua_var.ua_cfg.cb.on_stream_created) {
	    pjsua_var.ua_cfg.cb.on_stream_created(call->index,
						  call_med->strm.a.stream,
						  strm_idx, &media_port);
	}

	/*
	 * Add the call to conference bridge.
	 */
	{
	    char tmp[PJSIP_MAX_URL_SIZE];
	    pj_str_t port_name;

	    port_name.ptr = tmp;
	    port_name.slen = pjsip_uri_print(PJSIP_URI_IN_REQ_URI,
					     call->inv->dlg->remote.info->uri,
					     tmp, sizeof(tmp));
	    if (port_name.slen < 1) {
		port_name = pj_str("call");
	    }
	    status = pjmedia_conf_add_port( pjsua_var.mconf,
					    call->inv->pool_prov,
					    media_port,
					    &port_name,
					    (unsigned*)
					    &call_med->strm.a.conf_slot);
	    if (status != PJ_SUCCESS) {
		goto on_return;
	    }
	}
    }

on_return:
    pj_log_pop_indent();
    return status;
}


/*
 * Get maxinum number of conference ports.
 */
PJ_DEF(unsigned) pjsua_conf_get_max_ports(void)
{
    return pjsua_var.media_cfg.max_media_ports;
}


/*
 * Get current number of active ports in the bridge.
 */
PJ_DEF(unsigned) pjsua_conf_get_active_ports(void)
{
    unsigned ports[PJSUA_MAX_CONF_PORTS];
    unsigned count = PJ_ARRAY_SIZE(ports);
    pj_status_t status;

    status = pjmedia_conf_enum_ports(pjsua_var.mconf, ports, &count);
    if (status != PJ_SUCCESS)
	count = 0;

    return count;
}


/*
 * Enumerate all conference ports.
 */
PJ_DEF(pj_status_t) pjsua_enum_conf_ports(pjsua_conf_port_id id[],
					  unsigned *count)
{
    return pjmedia_conf_enum_ports(pjsua_var.mconf, (unsigned*)id, count);
}


/*
 * Get information about the specified conference port
 */
PJ_DEF(pj_status_t) pjsua_conf_get_port_info( pjsua_conf_port_id id,
					      pjsua_conf_port_info *info)
{
    pjmedia_conf_port_info cinfo;
    unsigned i;
    pj_status_t status;

    status = pjmedia_conf_get_port_info( pjsua_var.mconf, id, &cinfo);
    if (status != PJ_SUCCESS)
	return status;

    pj_bzero(info, sizeof(*info));
    info->slot_id = id;
    info->name = cinfo.name;
    info->clock_rate = cinfo.clock_rate;
    info->channel_count = cinfo.channel_count;
    info->samples_per_frame = cinfo.samples_per_frame;
    info->bits_per_sample = cinfo.bits_per_sample;

    /* Build array of listeners */
    info->listener_cnt = cinfo.listener_cnt;
    for (i=0; i<cinfo.listener_cnt; ++i) {
	info->listeners[i] = cinfo.listener_slots[i];
    }

    return PJ_SUCCESS;
}


/*
 * Add arbitrary media port to PJSUA's conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_conf_add_port( pj_pool_t *pool,
					 pjmedia_port *port,
					 pjsua_conf_port_id *p_id)
{
    pj_status_t status;

    status = pjmedia_conf_add_port(pjsua_var.mconf, pool,
				   port, NULL, (unsigned*)p_id);
    if (status != PJ_SUCCESS) {
	if (p_id)
	    *p_id = PJSUA_INVALID_ID;
    }

    return status;
}


/*
 * Remove arbitrary slot from the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_conf_remove_port(pjsua_conf_port_id id)
{
    pj_status_t status;

    status = pjmedia_conf_remove_port(pjsua_var.mconf, (unsigned)id);
    check_snd_dev_idle();

    return status;
}


/*
 * Establish unidirectional media flow from souce to sink.
 */
PJ_DEF(pj_status_t) pjsua_conf_connect( pjsua_conf_port_id source,
					pjsua_conf_port_id sink)
{
    pj_status_t status = PJ_SUCCESS;

    PJ_LOG(4,(THIS_FILE, "%s connect: %d --> %d",
	      (pjsua_var.is_mswitch ? "Switch" : "Conf"),
	      source, sink));
    pj_log_push_indent();

    PJSUA_LOCK();

    /* If sound device idle timer is active, cancel it first. */
    if (pjsua_var.snd_idle_timer.id) {
	pjsip_endpt_cancel_timer(pjsua_var.endpt, &pjsua_var.snd_idle_timer);
	pjsua_var.snd_idle_timer.id = PJ_FALSE;
    }


    /* For audio switchboard (i.e. APS-Direct):
     * Check if sound device need to be reopened, i.e: its attributes
     * (format, clock rate, channel count) must match to peer's.
     * Note that sound device can be reopened only if it doesn't have
     * any connection.
     */
    if (pjsua_var.is_mswitch) {
	pjmedia_conf_port_info port0_info;
	pjmedia_conf_port_info peer_info;
	unsigned peer_id;
	pj_bool_t need_reopen = PJ_FALSE;

	peer_id = (source!=0)? source : sink;
	status = pjmedia_conf_get_port_info(pjsua_var.mconf, peer_id,
					    &peer_info);
	pj_assert(status == PJ_SUCCESS);

	status = pjmedia_conf_get_port_info(pjsua_var.mconf, 0, &port0_info);
	pj_assert(status == PJ_SUCCESS);

	/* Check if sound device is instantiated. */
	need_reopen = (pjsua_var.snd_port==NULL && pjsua_var.null_snd==NULL &&
		      !pjsua_var.no_snd);

	/* Check if sound device need to reopen because it needs to modify
	 * settings to match its peer. Sound device must be idle in this case
	 * though.
	 */
	if (!need_reopen &&
	    port0_info.listener_cnt==0 && port0_info.transmitter_cnt==0)
	{
	    need_reopen = (peer_info.format.id != port0_info.format.id ||
			   peer_info.format.det.aud.avg_bps !=
				   port0_info.format.det.aud.avg_bps ||
			   peer_info.clock_rate != port0_info.clock_rate ||
			   peer_info.channel_count!=port0_info.channel_count);
	}

	if (need_reopen) {
	    if (pjsua_var.cap_dev != NULL_SND_DEV_ID) {
		pjmedia_snd_port_param param;

		pjmedia_snd_port_param_default(&param);
		param.ec_options = pjsua_var.media_cfg.ec_options;

		/* Create parameter based on peer info */
		status = create_aud_param(&param.base, pjsua_var.cap_dev,
					  pjsua_var.play_dev,
					  peer_info.clock_rate,
					  peer_info.channel_count,
					  peer_info.samples_per_frame,
					  peer_info.bits_per_sample);
		if (status != PJ_SUCCESS) {
		    pjsua_perror(THIS_FILE, "Error opening sound device",
				 status);
		    goto on_return;
		}

		/* And peer format */
		if (peer_info.format.id != PJMEDIA_FORMAT_PCM) {
		    param.base.flags |= PJMEDIA_AUD_DEV_CAP_EXT_FORMAT;
		    param.base.ext_fmt = peer_info.format;
		}

		param.options = 0;
		status = open_snd_dev(&param);
		if (status != PJ_SUCCESS) {
		    pjsua_perror(THIS_FILE, "Error opening sound device",
				 status);
		    goto on_return;
		}
	    } else {
		/* Null-audio */
		status = pjsua_set_snd_dev(pjsua_var.cap_dev,
					   pjsua_var.play_dev);
		if (status != PJ_SUCCESS) {
		    pjsua_perror(THIS_FILE, "Error opening sound device",
				 status);
		    goto on_return;
		}
	    }
	} else if (pjsua_var.no_snd) {
	    if (!pjsua_var.snd_is_on) {
		pjsua_var.snd_is_on = PJ_TRUE;
	    	/* Notify app */
	    	if (pjsua_var.ua_cfg.cb.on_snd_dev_operation) {
	    	    (*pjsua_var.ua_cfg.cb.on_snd_dev_operation)(1);
	    	}
	    }
	}

    } else {
	/* The bridge version */

	/* Create sound port if none is instantiated */
	if (pjsua_var.snd_port==NULL && pjsua_var.null_snd==NULL &&
	    !pjsua_var.no_snd)
	{
	    status = pjsua_set_snd_dev(pjsua_var.cap_dev, pjsua_var.play_dev);
	    if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "Error opening sound device", status);
		goto on_return;
	    }
	} else if (pjsua_var.no_snd && !pjsua_var.snd_is_on) {
	    pjsua_var.snd_is_on = PJ_TRUE;
	    /* Notify app */
	    if (pjsua_var.ua_cfg.cb.on_snd_dev_operation) {
		(*pjsua_var.ua_cfg.cb.on_snd_dev_operation)(1);
	    }
	}
    }

on_return:
    PJSUA_UNLOCK();

    if (status == PJ_SUCCESS) {
	status = pjmedia_conf_connect_port(pjsua_var.mconf, source, sink, 0);
    }

    pj_log_pop_indent();
    return status;
}


/*
 * Disconnect media flow from the source to destination port.
 */
PJ_DEF(pj_status_t) pjsua_conf_disconnect( pjsua_conf_port_id source,
					   pjsua_conf_port_id sink)
{
    pj_status_t status;

    PJ_LOG(4,(THIS_FILE, "%s disconnect: %d -x- %d",
	      (pjsua_var.is_mswitch ? "Switch" : "Conf"),
	      source, sink));
    pj_log_push_indent();

    status = pjmedia_conf_disconnect_port(pjsua_var.mconf, source, sink);
    check_snd_dev_idle();

    pj_log_pop_indent();
    return status;
}


/*
 * Adjust the signal level to be transmitted from the bridge to the
 * specified port by making it louder or quieter.
 */
PJ_DEF(pj_status_t) pjsua_conf_adjust_tx_level(pjsua_conf_port_id slot,
					       float level)
{
    return pjmedia_conf_adjust_tx_level(pjsua_var.mconf, slot,
					(int)((level-1) * 128));
}

/*
 * Adjust the signal level to be received from the specified port (to
 * the bridge) by making it louder or quieter.
 */
PJ_DEF(pj_status_t) pjsua_conf_adjust_rx_level(pjsua_conf_port_id slot,
					       float level)
{
    return pjmedia_conf_adjust_rx_level(pjsua_var.mconf, slot,
					(int)((level-1) * 128));
}


/*
 * Get last signal level transmitted to or received from the specified port.
 */
PJ_DEF(pj_status_t) pjsua_conf_get_signal_level(pjsua_conf_port_id slot,
						unsigned *tx_level,
						unsigned *rx_level)
{
    return pjmedia_conf_get_signal_level(pjsua_var.mconf, slot,
					 tx_level, rx_level);
}

/*****************************************************************************
 * File player.
 */

static char* get_basename(const char *path, unsigned len)
{
    char *p = ((char*)path) + len;

    if (len==0)
	return p;

    for (--p; p!=path && *p!='/' && *p!='\\'; ) --p;

    return (p==path) ? p : p+1;
}


/*
 * Create a file player, and automatically connect this player to
 * the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_player_create( const pj_str_t *filename,
					 unsigned options,
					 pjsua_player_id *p_id)
{
    unsigned slot, file_id;
    char path[PJ_MAXPATH];
    pj_pool_t *pool = NULL;
    pjmedia_port *port;
    pj_status_t status = PJ_SUCCESS;

    if (pjsua_var.player_cnt >= PJ_ARRAY_SIZE(pjsua_var.player))
	return PJ_ETOOMANY;

    PJ_LOG(4,(THIS_FILE, "Creating file player: %.*s..",
	      (int)filename->slen, filename->ptr));
    pj_log_push_indent();

    PJSUA_LOCK();

    for (file_id=0; file_id<PJ_ARRAY_SIZE(pjsua_var.player); ++file_id) {
	if (pjsua_var.player[file_id].port == NULL)
	    break;
    }

    if (file_id == PJ_ARRAY_SIZE(pjsua_var.player)) {
	/* This is unexpected */
	pj_assert(0);
	status = PJ_EBUG;
	goto on_error;
    }

    pj_memcpy(path, filename->ptr, filename->slen);
    path[filename->slen] = '\0';

    pool = pjsua_pool_create(get_basename(path, filename->slen), 1000, 1000);
    if (!pool) {
	status = PJ_ENOMEM;
	goto on_error;
    }

    status = pjmedia_wav_player_port_create(
				    pool, path,
				    pjsua_var.mconf_cfg.samples_per_frame *
				    1000 / pjsua_var.media_cfg.channel_count /
				    pjsua_var.media_cfg.clock_rate,
				    options, 0, &port);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to open file for playback", status);
	goto on_error;
    }

    status = pjmedia_conf_add_port(pjsua_var.mconf, pool,
				   port, filename, &slot);
    if (status != PJ_SUCCESS) {
	pjmedia_port_destroy(port);
	pjsua_perror(THIS_FILE, "Unable to add file to conference bridge",
		     status);
	goto on_error;
    }

    pjsua_var.player[file_id].type = 0;
    pjsua_var.player[file_id].pool = pool;
    pjsua_var.player[file_id].port = port;
    pjsua_var.player[file_id].slot = slot;

    if (p_id) *p_id = file_id;

    ++pjsua_var.player_cnt;

    PJSUA_UNLOCK();

    PJ_LOG(4,(THIS_FILE, "Player created, id=%d, slot=%d", file_id, slot));

    pj_log_pop_indent();
    return PJ_SUCCESS;

on_error:
    PJSUA_UNLOCK();
    if (pool) pj_pool_release(pool);
    pj_log_pop_indent();
    return status;
}


/*
 * Create a file playlist media port, and automatically add the port
 * to the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_playlist_create( const pj_str_t file_names[],
					   unsigned file_count,
					   const pj_str_t *label,
					   unsigned options,
					   pjsua_player_id *p_id)
{
    unsigned slot, file_id, ptime;
    pj_pool_t *pool = NULL;
    pjmedia_port *port;
    pj_status_t status = PJ_SUCCESS;

    if (pjsua_var.player_cnt >= PJ_ARRAY_SIZE(pjsua_var.player))
	return PJ_ETOOMANY;

    PJ_LOG(4,(THIS_FILE, "Creating playlist with %d file(s)..", file_count));
    pj_log_push_indent();

    PJSUA_LOCK();

    for (file_id=0; file_id<PJ_ARRAY_SIZE(pjsua_var.player); ++file_id) {
	if (pjsua_var.player[file_id].port == NULL)
	    break;
    }

    if (file_id == PJ_ARRAY_SIZE(pjsua_var.player)) {
	/* This is unexpected */
	pj_assert(0);
	status = PJ_EBUG;
	goto on_error;
    }


    ptime = pjsua_var.mconf_cfg.samples_per_frame * 1000 /
	    pjsua_var.media_cfg.clock_rate;

    pool = pjsua_pool_create("playlist", 1000, 1000);
    if (!pool) {
	status = PJ_ENOMEM;
	goto on_error;
    }

    status = pjmedia_wav_playlist_create(pool, label,
					 file_names, file_count,
					 ptime, options, 0, &port);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create playlist", status);
	goto on_error;
    }

    status = pjmedia_conf_add_port(pjsua_var.mconf, pool,
				   port, &port->info.name, &slot);
    if (status != PJ_SUCCESS) {
	pjmedia_port_destroy(port);
	pjsua_perror(THIS_FILE, "Unable to add port", status);
	goto on_error;
    }

    pjsua_var.player[file_id].type = 1;
    pjsua_var.player[file_id].pool = pool;
    pjsua_var.player[file_id].port = port;
    pjsua_var.player[file_id].slot = slot;

    if (p_id) *p_id = file_id;

    ++pjsua_var.player_cnt;

    PJSUA_UNLOCK();

    PJ_LOG(4,(THIS_FILE, "Playlist created, id=%d, slot=%d", file_id, slot));

    pj_log_pop_indent();

    return PJ_SUCCESS;

on_error:
    PJSUA_UNLOCK();
    if (pool) pj_pool_release(pool);
    pj_log_pop_indent();

    return status;
}


/*
 * Get conference port ID associated with player.
 */
PJ_DEF(pjsua_conf_port_id) pjsua_player_get_conf_port(pjsua_player_id id)
{
    PJ_ASSERT_RETURN(id>=0&&id<(int)PJ_ARRAY_SIZE(pjsua_var.player), PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].port != NULL, PJ_EINVAL);

    return pjsua_var.player[id].slot;
}

/*
 * Get the media port for the player.
 */
PJ_DEF(pj_status_t) pjsua_player_get_port( pjsua_player_id id,
					   pjmedia_port **p_port)
{
    PJ_ASSERT_RETURN(id>=0&&id<(int)PJ_ARRAY_SIZE(pjsua_var.player), PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].port != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(p_port != NULL, PJ_EINVAL);

    *p_port = pjsua_var.player[id].port;

    return PJ_SUCCESS;
}

/*
 * Set playback position.
 */
PJ_DEF(pj_status_t) pjsua_player_set_pos( pjsua_player_id id,
					  pj_uint32_t samples)
{
    PJ_ASSERT_RETURN(id>=0&&id<(int)PJ_ARRAY_SIZE(pjsua_var.player), PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].port != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].type == 0, PJ_EINVAL);

    return pjmedia_wav_player_port_set_pos(pjsua_var.player[id].port, samples);
}


/*
 * Close the file, remove the player from the bridge, and free
 * resources associated with the file player.
 */
PJ_DEF(pj_status_t) pjsua_player_destroy(pjsua_player_id id)
{
    PJ_ASSERT_RETURN(id>=0&&id<(int)PJ_ARRAY_SIZE(pjsua_var.player), PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].port != NULL, PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Destroying player %d..", id));
    pj_log_push_indent();

    PJSUA_LOCK();

    if (pjsua_var.player[id].port) {
	pjsua_conf_remove_port(pjsua_var.player[id].slot);
	pjmedia_port_destroy(pjsua_var.player[id].port);
	pjsua_var.player[id].port = NULL;
	pjsua_var.player[id].slot = 0xFFFF;
	pj_pool_release(pjsua_var.player[id].pool);
	pjsua_var.player[id].pool = NULL;
	pjsua_var.player_cnt--;
    }

    PJSUA_UNLOCK();
    pj_log_pop_indent();

    return PJ_SUCCESS;
}


/*****************************************************************************
 * File recorder.
 */

/*
 * Create a file recorder, and automatically connect this recorder to
 * the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_recorder_create( const pj_str_t *filename,
					   unsigned enc_type,
					   void *enc_param,
					   pj_ssize_t max_size,
					   unsigned options,
					   pjsua_recorder_id *p_id)
{
    enum Format
    {
	FMT_UNKNOWN,
	FMT_WAV,
	FMT_MP3,
    };
    unsigned slot, file_id;
    char path[PJ_MAXPATH];
    pj_str_t ext;
    int file_format;
    pj_pool_t *pool = NULL;
    pjmedia_port *port;
    pj_status_t status = PJ_SUCCESS;

    /* Filename must present */
    PJ_ASSERT_RETURN(filename != NULL, PJ_EINVAL);

    /* Don't support max_size at present */
    PJ_ASSERT_RETURN(max_size == 0 || max_size == -1, PJ_EINVAL);

    /* Don't support encoding type at present */
    PJ_ASSERT_RETURN(enc_type == 0, PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Creating recorder %.*s..",
	      (int)filename->slen, filename->ptr));
    pj_log_push_indent();

    if (pjsua_var.rec_cnt >= PJ_ARRAY_SIZE(pjsua_var.recorder)) {
	pj_log_pop_indent();
	return PJ_ETOOMANY;
    }

    /* Determine the file format */
    ext.ptr = filename->ptr + filename->slen - 4;
    ext.slen = 4;

    if (pj_stricmp2(&ext, ".wav") == 0)
	file_format = FMT_WAV;
    else if (pj_stricmp2(&ext, ".mp3") == 0)
	file_format = FMT_MP3;
    else {
	PJ_LOG(1,(THIS_FILE, "pjsua_recorder_create() error: unable to "
			     "determine file format for %.*s",
			     (int)filename->slen, filename->ptr));
	pj_log_pop_indent();
	return PJ_ENOTSUP;
    }

    PJSUA_LOCK();

    for (file_id=0; file_id<PJ_ARRAY_SIZE(pjsua_var.recorder); ++file_id) {
	if (pjsua_var.recorder[file_id].port == NULL)
	    break;
    }

    if (file_id == PJ_ARRAY_SIZE(pjsua_var.recorder)) {
	/* This is unexpected */
	pj_assert(0);
	status = PJ_EBUG;
	goto on_return;
    }

    pj_memcpy(path, filename->ptr, filename->slen);
    path[filename->slen] = '\0';

    pool = pjsua_pool_create(get_basename(path, filename->slen), 1000, 1000);
    if (!pool) {
	status = PJ_ENOMEM;
	goto on_return;
    }

    if (file_format == FMT_WAV) {
	status = pjmedia_wav_writer_port_create(pool, path,
						pjsua_var.media_cfg.clock_rate,
						pjsua_var.mconf_cfg.channel_count,
						pjsua_var.mconf_cfg.samples_per_frame,
						pjsua_var.mconf_cfg.bits_per_sample,
						options, 0, &port);
    } else {
	PJ_UNUSED_ARG(enc_param);
	port = NULL;
	status = PJ_ENOTSUP;
    }

    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to open file for recording", status);
	goto on_return;
    }

    status = pjmedia_conf_add_port(pjsua_var.mconf, pool,
				   port, filename, &slot);
    if (status != PJ_SUCCESS) {
	pjmedia_port_destroy(port);
	goto on_return;
    }

    pjsua_var.recorder[file_id].port = port;
    pjsua_var.recorder[file_id].slot = slot;
    pjsua_var.recorder[file_id].pool = pool;

    if (p_id) *p_id = file_id;

    ++pjsua_var.rec_cnt;

    PJSUA_UNLOCK();

    PJ_LOG(4,(THIS_FILE, "Recorder created, id=%d, slot=%d", file_id, slot));

    pj_log_pop_indent();
    return PJ_SUCCESS;

on_return:
    PJSUA_UNLOCK();
    if (pool) pj_pool_release(pool);
    pj_log_pop_indent();
    return status;
}


/*
 * Get conference port associated with recorder.
 */
PJ_DEF(pjsua_conf_port_id) pjsua_recorder_get_conf_port(pjsua_recorder_id id)
{
    PJ_ASSERT_RETURN(id>=0 && id<(int)PJ_ARRAY_SIZE(pjsua_var.recorder),
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.recorder[id].port != NULL, PJ_EINVAL);

    return pjsua_var.recorder[id].slot;
}

/*
 * Get the media port for the recorder.
 */
PJ_DEF(pj_status_t) pjsua_recorder_get_port( pjsua_recorder_id id,
					     pjmedia_port **p_port)
{
    PJ_ASSERT_RETURN(id>=0 && id<(int)PJ_ARRAY_SIZE(pjsua_var.recorder),
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.recorder[id].port != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(p_port != NULL, PJ_EINVAL);

    *p_port = pjsua_var.recorder[id].port;
    return PJ_SUCCESS;
}

/*
 * Destroy recorder (this will complete recording).
 */
PJ_DEF(pj_status_t) pjsua_recorder_destroy(pjsua_recorder_id id)
{
    PJ_ASSERT_RETURN(id>=0 && id<(int)PJ_ARRAY_SIZE(pjsua_var.recorder),
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.recorder[id].port != NULL, PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Destroying recorder %d..", id));
    pj_log_push_indent();

    PJSUA_LOCK();

    if (pjsua_var.recorder[id].port) {
	pjsua_conf_remove_port(pjsua_var.recorder[id].slot);
	pjmedia_port_destroy(pjsua_var.recorder[id].port);
	pjsua_var.recorder[id].port = NULL;
	pjsua_var.recorder[id].slot = 0xFFFF;
	pj_pool_release(pjsua_var.recorder[id].pool);
	pjsua_var.recorder[id].pool = NULL;
	pjsua_var.rec_cnt--;
    }

    PJSUA_UNLOCK();
    pj_log_pop_indent();

    return PJ_SUCCESS;
}


/*****************************************************************************
 * Sound devices.
 */

/*
 * Enum sound devices.
 */

PJ_DEF(pj_status_t) pjsua_enum_aud_devs( pjmedia_aud_dev_info info[],
					 unsigned *count)
{
    unsigned i, dev_count;

    dev_count = pjmedia_aud_dev_count();

    if (dev_count > *count) dev_count = *count;

    for (i=0; i<dev_count; ++i) {
	pj_status_t status;

	status = pjmedia_aud_dev_get_info(i, &info[i]);
	if (status != PJ_SUCCESS)
	    return status;
    }

    *count = dev_count;

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjsua_enum_snd_devs( pjmedia_snd_dev_info info[],
					 unsigned *count)
{
    unsigned i, dev_count;

    dev_count = pjmedia_aud_dev_count();

    if (dev_count > *count) dev_count = *count;
    pj_bzero(info, dev_count * sizeof(pjmedia_snd_dev_info));

    for (i=0; i<dev_count; ++i) {
	pjmedia_aud_dev_info ai;
	pj_status_t status;

	status = pjmedia_aud_dev_get_info(i, &ai);
	if (status != PJ_SUCCESS)
	    return status;

	strncpy(info[i].name, ai.name, sizeof(info[i].name));
	info[i].name[sizeof(info[i].name)-1] = '\0';
	info[i].input_count = ai.input_count;
	info[i].output_count = ai.output_count;
	info[i].default_samples_per_sec = ai.default_samples_per_sec;
    }

    *count = dev_count;

    return PJ_SUCCESS;
}

/* Create audio device parameter to open the device */
static pj_status_t create_aud_param(pjmedia_aud_param *param,
				    pjmedia_aud_dev_index capture_dev,
				    pjmedia_aud_dev_index playback_dev,
				    unsigned clock_rate,
				    unsigned channel_count,
				    unsigned samples_per_frame,
				    unsigned bits_per_sample)
{
    pj_status_t status;

    /* Normalize device ID with new convention about default device ID */
    if (playback_dev == PJMEDIA_AUD_DEFAULT_CAPTURE_DEV)
	playback_dev = PJMEDIA_AUD_DEFAULT_PLAYBACK_DEV;

    /* Create default parameters for the device */
    status = pjmedia_aud_dev_default_param(capture_dev, param);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error retrieving default audio "
				"device parameters", status);
	return status;
    }
    param->dir = PJMEDIA_DIR_CAPTURE_PLAYBACK;
    param->rec_id = capture_dev;
    param->play_id = playback_dev;
    param->clock_rate = clock_rate;
    param->channel_count = channel_count;
    param->samples_per_frame = samples_per_frame;
    param->bits_per_sample = bits_per_sample;

    /* Update the setting with user preference */
#define update_param(cap, field)    \
	if (pjsua_var.aud_param.flags & cap) { \
	    param->flags |= cap; \
	    param->field = pjsua_var.aud_param.field; \
	}
    update_param( PJMEDIA_AUD_DEV_CAP_INPUT_VOLUME_SETTING, input_vol);
    update_param( PJMEDIA_AUD_DEV_CAP_OUTPUT_VOLUME_SETTING, output_vol);
    update_param( PJMEDIA_AUD_DEV_CAP_INPUT_ROUTE, input_route);
    update_param( PJMEDIA_AUD_DEV_CAP_OUTPUT_ROUTE, output_route);
#undef update_param

    /* Latency settings */
    param->flags |= (PJMEDIA_AUD_DEV_CAP_INPUT_LATENCY |
		     PJMEDIA_AUD_DEV_CAP_OUTPUT_LATENCY);
    param->input_latency_ms = pjsua_var.media_cfg.snd_rec_latency;
    param->output_latency_ms = pjsua_var.media_cfg.snd_play_latency;

    /* EC settings */
    if (pjsua_var.media_cfg.ec_tail_len) {
	param->flags |= (PJMEDIA_AUD_DEV_CAP_EC | PJMEDIA_AUD_DEV_CAP_EC_TAIL);
	param->ec_enabled = PJ_TRUE;
	param->ec_tail_ms = pjsua_var.media_cfg.ec_tail_len;
    } else {
	param->flags &= ~(PJMEDIA_AUD_DEV_CAP_EC|PJMEDIA_AUD_DEV_CAP_EC_TAIL);
    }

    return PJ_SUCCESS;
}

/* Internal: the first time the audio device is opened (during app
 *   startup), retrieve the audio settings such as volume level
 *   so that aud_get_settings() will work.
 */
static pj_status_t update_initial_aud_param()
{
    pjmedia_aud_stream *strm;
    pjmedia_aud_param param;
    pj_status_t status;

    PJ_ASSERT_RETURN(pjsua_var.snd_port != NULL, PJ_EBUG);

    strm = pjmedia_snd_port_get_snd_stream(pjsua_var.snd_port);

    status = pjmedia_aud_stream_get_param(strm, &param);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error audio stream "
				"device parameters", status);
	return status;
    }

#define update_saved_param(cap, field)  \
	if (param.flags & cap) { \
	    pjsua_var.aud_param.flags |= cap; \
	    pjsua_var.aud_param.field = param.field; \
	}

    update_saved_param(PJMEDIA_AUD_DEV_CAP_INPUT_VOLUME_SETTING, input_vol);
    update_saved_param(PJMEDIA_AUD_DEV_CAP_OUTPUT_VOLUME_SETTING, output_vol);
    update_saved_param(PJMEDIA_AUD_DEV_CAP_INPUT_ROUTE, input_route);
    update_saved_param(PJMEDIA_AUD_DEV_CAP_OUTPUT_ROUTE, output_route);
#undef update_saved_param

    return PJ_SUCCESS;
}

/* Get format name */
static const char *get_fmt_name(pj_uint32_t id)
{
    static char name[8];

    if (id == PJMEDIA_FORMAT_L16)
	return "PCM";
    pj_memcpy(name, &id, 4);
    name[4] = '\0';
    return name;
}

/* Open sound device with the setting. */
static pj_status_t open_snd_dev(pjmedia_snd_port_param *param)
{
    pjmedia_port *conf_port;
    pj_status_t status;

    PJ_ASSERT_RETURN(param, PJ_EINVAL);

    /* Check if NULL sound device is used */
    if (NULL_SND_DEV_ID==param->base.rec_id ||
	NULL_SND_DEV_ID==param->base.play_id)
    {
	return pjsua_set_null_snd_dev();
    }

    /* Close existing sound port */
    close_snd_dev();

    /* Notify app */
    if (pjsua_var.ua_cfg.cb.on_snd_dev_operation) {
	(*pjsua_var.ua_cfg.cb.on_snd_dev_operation)(1);
    }

    /* Create memory pool for sound device. */
    pjsua_var.snd_pool = pjsua_pool_create("pjsua_snd", 4000, 4000);
    PJ_ASSERT_RETURN(pjsua_var.snd_pool, PJ_ENOMEM);


    PJ_LOG(4,(THIS_FILE, "Opening sound device %s@%d/%d/%dms",
	      get_fmt_name(param->base.ext_fmt.id),
	      param->base.clock_rate, param->base.channel_count,
	      param->base.samples_per_frame / param->base.channel_count *
	      1000 / param->base.clock_rate));
    pj_log_push_indent();

    status = pjmedia_snd_port_create2( pjsua_var.snd_pool,
				       param, &pjsua_var.snd_port);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Get the port0 of the conference bridge. */
    conf_port = pjmedia_conf_get_master_port(pjsua_var.mconf);
    pj_assert(conf_port != NULL);

    /* For conference bridge, resample if necessary if the bridge's
     * clock rate is different than the sound device's clock rate.
     */
    if (!pjsua_var.is_mswitch &&
	param->base.ext_fmt.id == PJMEDIA_FORMAT_PCM &&
	PJMEDIA_PIA_SRATE(&conf_port->info) != param->base.clock_rate)
    {
	pjmedia_port *resample_port;
	unsigned resample_opt = 0;

	if (pjsua_var.media_cfg.quality >= 3 &&
	    pjsua_var.media_cfg.quality <= 4)
	{
	    resample_opt |= PJMEDIA_RESAMPLE_USE_SMALL_FILTER;
	}
	else if (pjsua_var.media_cfg.quality < 3) {
	    resample_opt |= PJMEDIA_RESAMPLE_USE_LINEAR;
	}

	status = pjmedia_resample_port_create(pjsua_var.snd_pool,
					      conf_port,
					      param->base.clock_rate,
					      resample_opt,
					      &resample_port);
	if (status != PJ_SUCCESS) {
	    char errmsg[PJ_ERR_MSG_SIZE];
	    pj_strerror(status, errmsg, sizeof(errmsg));
	    PJ_LOG(4, (THIS_FILE,
		       "Error creating resample port: %s",
		       errmsg));
	    close_snd_dev();
	    goto on_error;
	}

	conf_port = resample_port;
    }

    /* Otherwise for audio switchboard, the switch's port0 setting is
     * derived from the sound device setting, so update the setting.
     */
    if (pjsua_var.is_mswitch) {
	if (param->base.flags & PJMEDIA_AUD_DEV_CAP_EXT_FORMAT) {
	    conf_port->info.fmt = param->base.ext_fmt;
	} else {
	    unsigned bps, ptime_usec;
	    bps = param->base.clock_rate * param->base.bits_per_sample;
	    ptime_usec = param->base.samples_per_frame /
			 param->base.channel_count * 1000000 /
			 param->base.clock_rate;
	    pjmedia_format_init_audio(&conf_port->info.fmt,
				      PJMEDIA_FORMAT_PCM,
				      param->base.clock_rate,
				      param->base.channel_count,
				      param->base.bits_per_sample,
				      ptime_usec,
				      bps, bps);
	}
    }


    /* Connect sound port to the bridge */
    status = pjmedia_snd_port_connect(pjsua_var.snd_port,
				      conf_port );
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to connect conference port to "
			        "sound device", status);
	pjmedia_snd_port_destroy(pjsua_var.snd_port);
	pjsua_var.snd_port = NULL;
	goto on_error;
    }

    /* Save the device IDs */
    pjsua_var.cap_dev = param->base.rec_id;
    pjsua_var.play_dev = param->base.play_id;

    /* Update sound device name. */
    {
	pjmedia_aud_dev_info rec_info;
	pjmedia_aud_stream *strm;
	pjmedia_aud_param si;
        pj_str_t tmp;

	strm = pjmedia_snd_port_get_snd_stream(pjsua_var.snd_port);
	status = pjmedia_aud_stream_get_param(strm, &si);
	if (status == PJ_SUCCESS)
	    status = pjmedia_aud_dev_get_info(si.rec_id, &rec_info);

	if (status==PJ_SUCCESS) {
	    if (param->base.clock_rate != pjsua_var.media_cfg.clock_rate) {
		char tmp_buf[128];
		int tmp_buf_len = sizeof(tmp_buf);

		tmp_buf_len = pj_ansi_snprintf(tmp_buf, sizeof(tmp_buf)-1,
					       "%s (%dKHz)",
					       rec_info.name,
					       param->base.clock_rate/1000);
		pj_strset(&tmp, tmp_buf, tmp_buf_len);
		pjmedia_conf_set_port0_name(pjsua_var.mconf, &tmp);
	    } else {
		pjmedia_conf_set_port0_name(pjsua_var.mconf,
					    pj_cstr(&tmp, rec_info.name));
	    }
	}

	/* Any error is not major, let it through */
	status = PJ_SUCCESS;
    }

    /* If this is the first time the audio device is open, retrieve some
     * settings from the device (such as volume settings) so that the
     * pjsua_snd_get_setting() work.
     */
    if (pjsua_var.aud_open_cnt == 0) {
	update_initial_aud_param();
	++pjsua_var.aud_open_cnt;
    }

    pjsua_var.snd_is_on = PJ_TRUE;

    pj_log_pop_indent();
    return PJ_SUCCESS;

on_error:
    pj_log_pop_indent();
    return status;
}


/* Close existing sound device */
static void close_snd_dev(void)
{
    pj_log_push_indent();

    /* Notify app */
    if (pjsua_var.snd_is_on && pjsua_var.ua_cfg.cb.on_snd_dev_operation) {
	(*pjsua_var.ua_cfg.cb.on_snd_dev_operation)(0);
    }

    /* Close sound device */
    if (pjsua_var.snd_port) {
	pjmedia_aud_dev_info cap_info, play_info;
	pjmedia_aud_stream *strm;
	pjmedia_aud_param param;

	strm = pjmedia_snd_port_get_snd_stream(pjsua_var.snd_port);
	pjmedia_aud_stream_get_param(strm, &param);

	if (pjmedia_aud_dev_get_info(param.rec_id, &cap_info) != PJ_SUCCESS)
	    cap_info.name[0] = '\0';
	if (pjmedia_aud_dev_get_info(param.play_id, &play_info) != PJ_SUCCESS)
	    play_info.name[0] = '\0';

	PJ_LOG(4,(THIS_FILE, "Closing %s sound playback device and "
			     "%s sound capture device",
			     play_info.name, cap_info.name));

	pjmedia_snd_port_disconnect(pjsua_var.snd_port);
	pjmedia_snd_port_destroy(pjsua_var.snd_port);
	pjsua_var.snd_port = NULL;
    }

    /* Close null sound device */
    if (pjsua_var.null_snd) {
	PJ_LOG(4,(THIS_FILE, "Closing null sound device.."));
	pjmedia_master_port_destroy(pjsua_var.null_snd, PJ_FALSE);
	pjsua_var.null_snd = NULL;
    }

    if (pjsua_var.snd_pool)
	pj_pool_release(pjsua_var.snd_pool);

    pjsua_var.snd_pool = NULL;
    pjsua_var.snd_is_on = PJ_FALSE;

    pj_log_pop_indent();
}


/*
 * Select or change sound device. Application may call this function at
 * any time to replace current sound device.
 */
PJ_DEF(pj_status_t) pjsua_set_snd_dev( int capture_dev,
				       int playback_dev)
{
    unsigned alt_cr_cnt = 1;
    unsigned alt_cr[] = {0, 44100, 48000, 32000, 16000, 8000};
    unsigned i;
    pj_status_t status = -1;

    PJ_LOG(4,(THIS_FILE, "Set sound device: capture=%d, playback=%d",
	      capture_dev, playback_dev));
    pj_log_push_indent();

    PJSUA_LOCK();

    /* Null-sound */
    if (capture_dev==NULL_SND_DEV_ID && playback_dev==NULL_SND_DEV_ID) {
	PJSUA_UNLOCK();
	status = pjsua_set_null_snd_dev();
	pj_log_pop_indent();
	return status;
    }

    /* Set default clock rate */
    alt_cr[0] = pjsua_var.media_cfg.snd_clock_rate;
    if (alt_cr[0] == 0)
	alt_cr[0] = pjsua_var.media_cfg.clock_rate;

    /* Allow retrying of different clock rate if we're using conference
     * bridge (meaning audio format is always PCM), otherwise lock on
     * to one clock rate.
     */
    if (pjsua_var.is_mswitch) {
	alt_cr_cnt = 1;
    } else {
	alt_cr_cnt = PJ_ARRAY_SIZE(alt_cr);
    }

    /* Attempts to open the sound device with different clock rates */
    for (i=0; i<alt_cr_cnt; ++i) {
	pjmedia_snd_port_param param;
	unsigned samples_per_frame;

	/* Create the default audio param */
	samples_per_frame = alt_cr[i] *
			    pjsua_var.media_cfg.audio_frame_ptime *
			    pjsua_var.media_cfg.channel_count / 1000;
	pjmedia_snd_port_param_default(&param);
	param.ec_options = pjsua_var.media_cfg.ec_options;
	status = create_aud_param(&param.base, capture_dev, playback_dev,
				  alt_cr[i], pjsua_var.media_cfg.channel_count,
				  samples_per_frame, 16);
	if (status != PJ_SUCCESS)
	    goto on_error;

	/* Open! */
	param.options = 0;
	status = open_snd_dev(&param);
	if (status == PJ_SUCCESS)
	    break;
    }

    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to open sound device", status);
	goto on_error;
    }

    pjsua_var.no_snd = PJ_FALSE;
    pjsua_var.snd_is_on = PJ_TRUE;

    PJSUA_UNLOCK();
    pj_log_pop_indent();
    return PJ_SUCCESS;

on_error:
    PJSUA_UNLOCK();
    pj_log_pop_indent();
    return status;
}


/*
 * Get currently active sound devices. If sound devices has not been created
 * (for example when pjsua_start() is not called), it is possible that
 * the function returns PJ_SUCCESS with -1 as device IDs.
 */
PJ_DEF(pj_status_t) pjsua_get_snd_dev(int *capture_dev,
				      int *playback_dev)
{
    PJSUA_LOCK();

    if (capture_dev) {
	*capture_dev = pjsua_var.cap_dev;
    }
    if (playback_dev) {
	*playback_dev = pjsua_var.play_dev;
    }

    PJSUA_UNLOCK();
    return PJ_SUCCESS;
}


/*
 * Use null sound device.
 */
PJ_DEF(pj_status_t) pjsua_set_null_snd_dev(void)
{
    pjmedia_port *conf_port;
    pj_status_t status;

    PJ_LOG(4,(THIS_FILE, "Setting null sound device.."));
    pj_log_push_indent();

    PJSUA_LOCK();

    /* Close existing sound device */
    close_snd_dev();

    /* Notify app */
    if (pjsua_var.ua_cfg.cb.on_snd_dev_operation) {
	(*pjsua_var.ua_cfg.cb.on_snd_dev_operation)(1);
    }

    /* Create memory pool for sound device. */
    pjsua_var.snd_pool = pjsua_pool_create("pjsua_snd", 4000, 4000);
    PJ_ASSERT_RETURN(pjsua_var.snd_pool, PJ_ENOMEM);

    PJ_LOG(4,(THIS_FILE, "Opening null sound device.."));

    /* Get the port0 of the conference bridge. */
    conf_port = pjmedia_conf_get_master_port(pjsua_var.mconf);
    pj_assert(conf_port != NULL);

    /* Create master port, connecting port0 of the conference bridge to
     * a null port.
     */
    status = pjmedia_master_port_create(pjsua_var.snd_pool, pjsua_var.null_port,
					conf_port, 0, &pjsua_var.null_snd);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create null sound device",
		     status);
	PJSUA_UNLOCK();
	pj_log_pop_indent();
	return status;
    }

    /* Start the master port */
    status = pjmedia_master_port_start(pjsua_var.null_snd);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

    pjsua_var.cap_dev = NULL_SND_DEV_ID;
    pjsua_var.play_dev = NULL_SND_DEV_ID;

    pjsua_var.no_snd = PJ_FALSE;
    pjsua_var.snd_is_on = PJ_TRUE;

    PJSUA_UNLOCK();
    pj_log_pop_indent();
    return PJ_SUCCESS;
}



/*
 * Use no device!
 */
PJ_DEF(pjmedia_port*) pjsua_set_no_snd_dev(void)
{
    PJSUA_LOCK();

    /* Close existing sound device */
    close_snd_dev();
    pjsua_var.no_snd = PJ_TRUE;

    PJSUA_UNLOCK();

    return pjmedia_conf_get_master_port(pjsua_var.mconf);
}


/*
 * Configure the AEC settings of the sound port.
 */
PJ_DEF(pj_status_t) pjsua_set_ec(unsigned tail_ms, unsigned options)
{
    pj_status_t status = PJ_SUCCESS;

    PJSUA_LOCK();

    pjsua_var.media_cfg.ec_tail_len = tail_ms;
    pjsua_var.media_cfg.ec_options = options;

    if (pjsua_var.snd_port)
	status = pjmedia_snd_port_set_ec(pjsua_var.snd_port, pjsua_var.pool,
					 tail_ms, options);

    PJSUA_UNLOCK();
    return status;
}


/*
 * Get current AEC tail length.
 */
PJ_DEF(pj_status_t) pjsua_get_ec_tail(unsigned *p_tail_ms)
{
    *p_tail_ms = pjsua_var.media_cfg.ec_tail_len;
    return PJ_SUCCESS;
}


/*
 * Check whether the sound device is currently active.
 */
PJ_DEF(pj_bool_t) pjsua_snd_is_active(void)
{
    return pjsua_var.snd_port != NULL;
}


/*
 * Configure sound device setting to the sound device being used.
 */
PJ_DEF(pj_status_t) pjsua_snd_set_setting( pjmedia_aud_dev_cap cap,
					   const void *pval,
					   pj_bool_t keep)
{
    pj_status_t status;

    /* Check if we are allowed to set the cap */
    if ((cap & pjsua_var.aud_svmask) == 0) {
	return PJMEDIA_EAUD_INVCAP;
    }

    PJSUA_LOCK();

    /* If sound is active, set it immediately */
    if (pjsua_snd_is_active()) {
	pjmedia_aud_stream *strm;

	strm = pjmedia_snd_port_get_snd_stream(pjsua_var.snd_port);
	status = pjmedia_aud_stream_set_cap(strm, cap, pval);
    } else {
	status = PJ_SUCCESS;
    }

    if (status != PJ_SUCCESS) {
	PJSUA_UNLOCK();
	return status;
    }

    /* Save in internal param for later device open */
    if (keep) {
	status = pjmedia_aud_param_set_cap(&pjsua_var.aud_param,
					   cap, pval);
    }

    PJSUA_UNLOCK();
    return status;
}

/*
 * Retrieve a sound device setting.
 */
PJ_DEF(pj_status_t) pjsua_snd_get_setting( pjmedia_aud_dev_cap cap,
					   void *pval)
{
    pj_status_t status;

    PJSUA_LOCK();

    /* If sound device has never been opened before, open it to
     * retrieve the initial setting from the device (e.g. audio
     * volume)
     */
    if (pjsua_var.aud_open_cnt==0) {
	PJ_LOG(4,(THIS_FILE, "Opening sound device to get initial settings"));
	pjsua_set_snd_dev(pjsua_var.cap_dev, pjsua_var.play_dev);
	close_snd_dev();
    }

    if (pjsua_snd_is_active()) {
	/* Sound is active, retrieve from device directly */
	pjmedia_aud_stream *strm;

	strm = pjmedia_snd_port_get_snd_stream(pjsua_var.snd_port);
	status = pjmedia_aud_stream_get_cap(strm, cap, pval);
    } else {
	/* Otherwise retrieve from internal param */
	status = pjmedia_aud_param_get_cap(&pjsua_var.aud_param,
					   cap, pval);
    }

    PJSUA_UNLOCK();
    return status;
}

#endif /* PJSUA_MEDIA_HAS_PJMEDIA */
