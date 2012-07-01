/* $Id$ */
/* 
 * Copyright (C) 2011-2011 Teluu Inc. (http://www.teluu.com)
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

#define THIS_FILE	"pjsua_vid.c"

#if PJSUA_HAS_VIDEO

#define ENABLE_EVENT	    	1
#define VID_TEE_MAX_PORT    	(PJSUA_MAX_CALLS + 1)

#define PJSUA_SHOW_WINDOW	1
#define PJSUA_HIDE_WINDOW	0


static void free_vid_win(pjsua_vid_win_id wid);

/*****************************************************************************
 * pjsua video subsystem.
 */
pj_status_t pjsua_vid_subsys_init(void)
{
    unsigned i;
    pj_status_t status;

    PJ_LOG(4,(THIS_FILE, "Initializing video subsystem.."));
    pj_log_push_indent();

    status = pjmedia_video_format_mgr_create(pjsua_var.pool, 64, 0, NULL);
    if (status != PJ_SUCCESS) {
	PJ_PERROR(1,(THIS_FILE, status,
		     "Error creating PJMEDIA video format manager"));
	goto on_error;
    }

    status = pjmedia_converter_mgr_create(pjsua_var.pool, NULL);
    if (status != PJ_SUCCESS) {
	PJ_PERROR(1,(THIS_FILE, status,
		     "Error creating PJMEDIA converter manager"));
	goto on_error;
    }

    status = pjmedia_event_mgr_create(pjsua_var.pool, 0, NULL);
    if (status != PJ_SUCCESS) {
	PJ_PERROR(1,(THIS_FILE, status,
		     "Error creating PJMEDIA event manager"));
	goto on_error;
    }

    status = pjmedia_vid_codec_mgr_create(pjsua_var.pool, NULL);
    if (status != PJ_SUCCESS) {
	PJ_PERROR(1,(THIS_FILE, status,
		     "Error creating PJMEDIA video codec manager"));
	goto on_error;
    }

#if PJMEDIA_HAS_VIDEO && PJMEDIA_HAS_FFMPEG_VID_CODEC
    status = pjmedia_codec_ffmpeg_vid_init(NULL, &pjsua_var.cp.factory);
    if (status != PJ_SUCCESS) {
	PJ_PERROR(1,(THIS_FILE, status,
		     "Error initializing ffmpeg library"));
	goto on_error;
    }
#endif

    status = pjmedia_vid_dev_subsys_init(&pjsua_var.cp.factory);
    if (status != PJ_SUCCESS) {
	PJ_PERROR(1,(THIS_FILE, status,
		     "Error creating PJMEDIA video subsystem"));
	goto on_error;
    }

    for (i=0; i<PJSUA_MAX_VID_WINS; ++i) {
	if (pjsua_var.win[i].pool == NULL) {
	    pjsua_var.win[i].pool = pjsua_pool_create("win%p", 512, 512);
	    if (pjsua_var.win[i].pool == NULL) {
		status = PJ_ENOMEM;
		goto on_error;
	    }
	}
    }

    pj_log_pop_indent();
    return PJ_SUCCESS;

on_error:
    pj_log_pop_indent();
    return status;
}

pj_status_t pjsua_vid_subsys_start(void)
{
    return PJ_SUCCESS;
}

pj_status_t pjsua_vid_subsys_destroy(void)
{
    unsigned i;

    PJ_LOG(4,(THIS_FILE, "Destroying video subsystem.."));
    pj_log_push_indent();

    for (i=0; i<PJSUA_MAX_VID_WINS; ++i) {
	if (pjsua_var.win[i].pool) {
	    free_vid_win(i);
	    pj_pool_release(pjsua_var.win[i].pool);
	    pjsua_var.win[i].pool = NULL;
	}
    }

    pjmedia_vid_dev_subsys_shutdown();

#if PJMEDIA_HAS_FFMPEG_VID_CODEC
    pjmedia_codec_ffmpeg_vid_deinit();
#endif

    if (pjmedia_vid_codec_mgr_instance())
	pjmedia_vid_codec_mgr_destroy(NULL);

    if (pjmedia_converter_mgr_instance())
	pjmedia_converter_mgr_destroy(NULL);

    if (pjmedia_event_mgr_instance())
	pjmedia_event_mgr_destroy(NULL);

    if (pjmedia_video_format_mgr_instance())
	pjmedia_video_format_mgr_destroy(NULL);

    pj_log_pop_indent();
    return PJ_SUCCESS;
}

PJ_DEF(const char*) pjsua_vid_win_type_name(pjsua_vid_win_type wt)
{
    const char *win_type_names[] = {
         "none",
         "preview",
         "stream"
    };

    return (wt < PJ_ARRAY_SIZE(win_type_names)) ? win_type_names[wt] : "??";
}

PJ_DEF(void)
pjsua_call_vid_strm_op_param_default(pjsua_call_vid_strm_op_param *param)
{
    pj_bzero(param, sizeof(*param));
    param->med_idx = -1;
    param->dir = PJMEDIA_DIR_ENCODING_DECODING;
    param->cap_dev = PJMEDIA_VID_DEFAULT_CAPTURE_DEV;
}

PJ_DEF(void) pjsua_vid_preview_param_default(pjsua_vid_preview_param *p)
{
    p->rend_id = PJMEDIA_VID_DEFAULT_RENDER_DEV;
    p->show = PJ_TRUE;
    p->wnd_flags = 0;
}


/*****************************************************************************
 * Devices.
 */

/*
 * Get the number of video devices installed in the system.
 */
PJ_DEF(unsigned) pjsua_vid_dev_count(void)
{
    return pjmedia_vid_dev_count();
}

/*
 * Retrieve the video device info for the specified device index.
 */
PJ_DEF(pj_status_t) pjsua_vid_dev_get_info(pjmedia_vid_dev_index id,
                                           pjmedia_vid_dev_info *vdi)
{
    return pjmedia_vid_dev_get_info(id, vdi);
}

/*
 * Enum all video devices installed in the system.
 */
PJ_DEF(pj_status_t) pjsua_vid_enum_devs(pjmedia_vid_dev_info info[],
					unsigned *count)
{
    unsigned i, dev_count;

    dev_count = pjmedia_vid_dev_count();

    if (dev_count > *count) dev_count = *count;

    for (i=0; i<dev_count; ++i) {
	pj_status_t status;

	status = pjmedia_vid_dev_get_info(i, &info[i]);
	if (status != PJ_SUCCESS)
	    return status;
    }

    *count = dev_count;

    return PJ_SUCCESS;
}


/*****************************************************************************
 * Codecs.
 */

static pj_status_t find_codecs_with_rtp_packing(
				    const pj_str_t *codec_id,
				    unsigned *count,
				    const pjmedia_vid_codec_info *p_info[])
{
    const pjmedia_vid_codec_info *info[32];
    unsigned i, j, count_ = PJ_ARRAY_SIZE(info);
    pj_status_t status;
    
    status = pjmedia_vid_codec_mgr_find_codecs_by_id(NULL, codec_id,
						     &count_, info, NULL);
    if (status != PJ_SUCCESS)
	return status;

    for (i = 0, j = 0; i < count_ && j<*count; ++i) {
	if ((info[i]->packings & PJMEDIA_VID_PACKING_PACKETS) == 0)
	    continue;
	p_info[j++] = info[i];
    }
    *count = j;
    return PJ_SUCCESS;
}

/*
 * Enum all supported video codecs in the system.
 */
PJ_DEF(pj_status_t) pjsua_vid_enum_codecs( pjsua_codec_info id[],
					   unsigned *p_count )
{
    pjmedia_vid_codec_info info[32];
    unsigned i, j, count, prio[32];
    pj_status_t status;

    count = PJ_ARRAY_SIZE(info);
    status = pjmedia_vid_codec_mgr_enum_codecs(NULL, &count, info, prio);
    if (status != PJ_SUCCESS) {
	*p_count = 0;
	return status;
    }

    for (i=0, j=0; i<count && j<*p_count; ++i) {
	if (info[i].packings & PJMEDIA_VID_PACKING_PACKETS) {
	    pj_bzero(&id[j], sizeof(pjsua_codec_info));

	    pjmedia_vid_codec_info_to_id(&info[i], id[j].buf_, sizeof(id[j].buf_));
	    id[j].codec_id = pj_str(id[j].buf_);
	    id[j].priority = (pj_uint8_t) prio[i];

	    if (id[j].codec_id.slen < sizeof(id[j].buf_)) {
		id[j].desc.ptr = id[j].codec_id.ptr + id[j].codec_id.slen + 1;
		pj_strncpy(&id[j].desc, &info[i].encoding_desc,
			   sizeof(id[j].buf_) - id[j].codec_id.slen - 1);
	    }

	    ++j;
	}
    }

    *p_count = j;

    return PJ_SUCCESS;
}


/*
 * Change video codec priority.
 */
PJ_DEF(pj_status_t) pjsua_vid_codec_set_priority( const pj_str_t *codec_id,
						  pj_uint8_t priority )
{
    const pj_str_t all = { NULL, 0 };

    if (codec_id->slen==1 && *codec_id->ptr=='*')
	codec_id = &all;

    return pjmedia_vid_codec_mgr_set_codec_priority(NULL, codec_id,
						    priority);
}


/*
 * Get video codec parameters.
 */
PJ_DEF(pj_status_t) pjsua_vid_codec_get_param(
					const pj_str_t *codec_id,
					pjmedia_vid_codec_param *param)
{
    const pjmedia_vid_codec_info *info[2];
    unsigned count = 2;
    pj_status_t status;

    status = find_codecs_with_rtp_packing(codec_id, &count, info);
    if (status != PJ_SUCCESS)
	return status;

    if (count != 1)
	return (count > 1? PJ_ETOOMANY : PJ_ENOTFOUND);

    status = pjmedia_vid_codec_mgr_get_default_param(NULL, info[0], param);
    return status;
}


/*
 * Set video codec parameters.
 */
PJ_DEF(pj_status_t) pjsua_vid_codec_set_param(
					const pj_str_t *codec_id,
					const pjmedia_vid_codec_param *param)
{
    const pjmedia_vid_codec_info *info[2];
    unsigned count = 2;
    pj_status_t status;

    status = find_codecs_with_rtp_packing(codec_id, &count, info);
    if (status != PJ_SUCCESS)
	return status;

    if (count != 1)
	return (count > 1? PJ_ETOOMANY : PJ_ENOTFOUND);

    status = pjmedia_vid_codec_mgr_set_default_param(NULL, info[0], param);
    return status;
}


/*****************************************************************************
 * Preview
 */

static pjsua_vid_win_id vid_preview_get_win(pjmedia_vid_dev_index id,
                                            pj_bool_t running_only)
{
    pjsua_vid_win_id wid = PJSUA_INVALID_ID;
    unsigned i;

    PJSUA_LOCK();

    /* Get real capture ID, if set to PJMEDIA_VID_DEFAULT_CAPTURE_DEV */
    if (id == PJMEDIA_VID_DEFAULT_CAPTURE_DEV) {
	pjmedia_vid_dev_info info;
	pjmedia_vid_dev_get_info(id, &info);
	id = info.id;
    }

    for (i=0; i<PJSUA_MAX_VID_WINS; ++i) {
	pjsua_vid_win *w = &pjsua_var.win[i];
	if (w->type == PJSUA_WND_TYPE_PREVIEW && w->preview_cap_id == id) {
	    wid = i;
	    break;
	}
    }

    if (wid != PJSUA_INVALID_ID && running_only) {
	pjsua_vid_win *w = &pjsua_var.win[wid];
	wid = w->preview_running ? wid : PJSUA_INVALID_ID;
    }

    PJSUA_UNLOCK();

    return wid;
}

/*
 * NOTE: internal function don't use this!!! Use vid_preview_get_win()
 *       instead. This is because this function will only return window ID
 *       if preview is currently running.
 */
PJ_DEF(pjsua_vid_win_id) pjsua_vid_preview_get_win(pjmedia_vid_dev_index id)
{
    return vid_preview_get_win(id, PJ_TRUE);
}

PJ_DEF(void) pjsua_vid_win_reset(pjsua_vid_win_id wid)
{
    pjsua_vid_win *w = &pjsua_var.win[wid];
    pj_pool_t *pool = w->pool;

    pj_bzero(w, sizeof(*w));
    if (pool) pj_pool_reset(pool);
    w->ref_cnt = 0;
    w->pool = pool;
    w->preview_cap_id = PJMEDIA_VID_INVALID_DEV;
}

/* Allocate and initialize pjsua video window:
 * - If the type is preview, video capture, tee, and render
 *   will be instantiated.
 * - If the type is stream, only renderer will be created.
 */
static pj_status_t create_vid_win(pjsua_vid_win_type type,
				  const pjmedia_format *fmt,
				  pjmedia_vid_dev_index rend_id,
				  pjmedia_vid_dev_index cap_id,
				  pj_bool_t show,
                                  unsigned wnd_flags,
				  pjsua_vid_win_id *id)
{
    pj_bool_t enable_native_preview;
    pjsua_vid_win_id wid = PJSUA_INVALID_ID;
    pjsua_vid_win *w = NULL;
    pjmedia_vid_port_param vp_param;
    pjmedia_format fmt_;
    pj_status_t status;
    unsigned i;

    enable_native_preview = pjsua_var.media_cfg.vid_preview_enable_native;

    PJ_LOG(4,(THIS_FILE,
	      "Creating video window: type=%s, cap_id=%d, rend_id=%d",
	      pjsua_vid_win_type_name(type), cap_id, rend_id));
    pj_log_push_indent();

    /* If type is preview, check if it exists already */
    if (type == PJSUA_WND_TYPE_PREVIEW) {
	wid = vid_preview_get_win(cap_id, PJ_FALSE);
	if (wid != PJSUA_INVALID_ID) {
	    /* Yes, it exists */
	    /* Show/hide window */
	    pjmedia_vid_dev_stream *strm;
	    pj_bool_t hide = !show;

	    w = &pjsua_var.win[wid];

	    PJ_LOG(4,(THIS_FILE,
		      "Window already exists for cap_dev=%d, returning wid=%d",
		      cap_id, wid));


	    if (w->is_native) {
		strm = pjmedia_vid_port_get_stream(w->vp_cap);
	    } else {
		strm = pjmedia_vid_port_get_stream(w->vp_rend);
	    }

	    pj_assert(strm);
	    status = pjmedia_vid_dev_stream_set_cap(
				    strm, PJMEDIA_VID_DEV_CAP_OUTPUT_HIDE,
				    &hide);

	    pjmedia_vid_dev_stream_set_cap(
                                strm, PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW_FLAGS,
				&wnd_flags);

	    /* Done */
	    *id = wid;
	    pj_log_pop_indent();

	    return status;
	}
    }

    /* Allocate window */
    for (i=0; i<PJSUA_MAX_VID_WINS; ++i) {
	w = &pjsua_var.win[i];
	if (w->type == PJSUA_WND_TYPE_NONE) {
	    wid = i;
	    w->type = type;
	    break;
	}
    }
    if (i == PJSUA_MAX_VID_WINS) {
	pj_log_pop_indent();
	return PJ_ETOOMANY;
    }

    /* Initialize window */
    pjmedia_vid_port_param_default(&vp_param);

    if (w->type == PJSUA_WND_TYPE_PREVIEW) {
	pjmedia_vid_dev_info vdi;

	/*
	 * Determine if the device supports native preview.
	 */
	status = pjmedia_vid_dev_get_info(cap_id, &vdi);
	if (status != PJ_SUCCESS)
	    goto on_error;

	if (enable_native_preview &&
	     (vdi.caps & PJMEDIA_VID_DEV_CAP_INPUT_PREVIEW))
	{
	    /* Device supports native preview! */
	    w->is_native = PJ_TRUE;
	}

	status = pjmedia_vid_dev_default_param(w->pool, cap_id,
					       &vp_param.vidparam);
	if (status != PJ_SUCCESS)
	    goto on_error;

	if (w->is_native) {
	    vp_param.vidparam.flags |= PJMEDIA_VID_DEV_CAP_OUTPUT_HIDE;
	    vp_param.vidparam.window_hide = !show;
	    vp_param.vidparam.flags |= PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW_FLAGS;
	    vp_param.vidparam.window_flags = wnd_flags;
	}

	/* Normalize capture ID, in case it was set to
	 * PJMEDIA_VID_DEFAULT_CAPTURE_DEV
	 */
	cap_id = vp_param.vidparam.cap_id;

	/* Assign preview capture device ID */
	w->preview_cap_id = cap_id;

	/* Create capture video port */
	vp_param.active = PJ_TRUE;
	vp_param.vidparam.dir = PJMEDIA_DIR_CAPTURE;
	if (fmt)
	    vp_param.vidparam.fmt = *fmt;

	status = pjmedia_vid_port_create(w->pool, &vp_param, &w->vp_cap);
	if (status != PJ_SUCCESS)
	    goto on_error;

	/* Update format info */
	fmt_ = vp_param.vidparam.fmt;
	fmt = &fmt_;

	/* Create video tee */
	status = pjmedia_vid_tee_create(w->pool, fmt, VID_TEE_MAX_PORT,
					&w->tee);
	if (status != PJ_SUCCESS)
	    goto on_error;

	/* Connect capturer to the video tee */
	status = pjmedia_vid_port_connect(w->vp_cap, w->tee, PJ_FALSE);
	if (status != PJ_SUCCESS)
	    goto on_error;

	/* If device supports native preview, enable it */
	if (w->is_native) {
	    pjmedia_vid_dev_stream *cap_dev;
	    pj_bool_t enabled = PJ_TRUE;

	    cap_dev = pjmedia_vid_port_get_stream(w->vp_cap);
	    status = pjmedia_vid_dev_stream_set_cap(
			    cap_dev, PJMEDIA_VID_DEV_CAP_INPUT_PREVIEW,
			    &enabled);
	    if (status != PJ_SUCCESS) {
		PJ_PERROR(1,(THIS_FILE, status,
			     "Error activating native preview, falling back "
			     "to software preview.."));
		w->is_native = PJ_FALSE;
	    }
	}
    }

    /* Create renderer video port, only if it's not a native preview */
    if (!w->is_native) {
	status = pjmedia_vid_dev_default_param(w->pool, rend_id,
					       &vp_param.vidparam);
	if (status != PJ_SUCCESS)
	    goto on_error;

	vp_param.active = (w->type == PJSUA_WND_TYPE_STREAM);
	vp_param.vidparam.dir = PJMEDIA_DIR_RENDER;
	vp_param.vidparam.fmt = *fmt;
	vp_param.vidparam.disp_size = fmt->det.vid.size;
	vp_param.vidparam.flags |= PJMEDIA_VID_DEV_CAP_OUTPUT_HIDE;
	vp_param.vidparam.window_hide = !show;
        vp_param.vidparam.flags |= PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW_FLAGS;
        vp_param.vidparam.window_flags = wnd_flags;

	status = pjmedia_vid_port_create(w->pool, &vp_param, &w->vp_rend);
	if (status != PJ_SUCCESS)
	    goto on_error;

	/* For preview window, connect capturer & renderer (via tee) */
	if (w->type == PJSUA_WND_TYPE_PREVIEW) {
	    pjmedia_port *rend_port;

	    rend_port = pjmedia_vid_port_get_passive_port(w->vp_rend);
	    status = pjmedia_vid_tee_add_dst_port2(w->tee, 0, rend_port);
	    if (status != PJ_SUCCESS)
		goto on_error;
	}

	PJ_LOG(4,(THIS_FILE,
		  "%s window id %d created for cap_dev=%d rend_dev=%d",
		  pjsua_vid_win_type_name(type), wid, cap_id, rend_id));
    } else {
	PJ_LOG(4,(THIS_FILE,
		  "Preview window id %d created for cap_dev %d, "
		  "using built-in preview!",
		  wid, cap_id));
    }


    /* Done */
    *id = wid;

    PJ_LOG(4,(THIS_FILE, "Window %d created", wid));
    pj_log_pop_indent();
    return PJ_SUCCESS;

on_error:
    free_vid_win(wid);
    pj_log_pop_indent();
    return status;
}


static void free_vid_win(pjsua_vid_win_id wid)
{
    pjsua_vid_win *w = &pjsua_var.win[wid];
    
    PJ_LOG(4,(THIS_FILE, "Window %d: destroying..", wid));
    pj_log_push_indent();

    if (w->vp_cap) {
        pjmedia_event_unsubscribe(NULL, &call_media_on_event, NULL,
                                  w->vp_cap);
	pjmedia_vid_port_stop(w->vp_cap);
	pjmedia_vid_port_disconnect(w->vp_cap);
	pjmedia_vid_port_destroy(w->vp_cap);
    }
    if (w->vp_rend) {
        pjmedia_event_unsubscribe(NULL, &call_media_on_event, NULL,
                                  w->vp_rend);
	pjmedia_vid_port_stop(w->vp_rend);
	pjmedia_vid_port_destroy(w->vp_rend);
    }
    if (w->tee) {
	pjmedia_port_destroy(w->tee);
    }
    pjsua_vid_win_reset(wid);

    pj_log_pop_indent();
}


static void inc_vid_win(pjsua_vid_win_id wid)
{
    pjsua_vid_win *w;
    
    pj_assert(wid >= 0 && wid < PJSUA_MAX_VID_WINS);

    w = &pjsua_var.win[wid];
    pj_assert(w->type != PJSUA_WND_TYPE_NONE);
    ++w->ref_cnt;
}

static void dec_vid_win(pjsua_vid_win_id wid)
{
    pjsua_vid_win *w;
    
    pj_assert(wid >= 0 && wid < PJSUA_MAX_VID_WINS);

    w = &pjsua_var.win[wid];
    pj_assert(w->type != PJSUA_WND_TYPE_NONE);
    if (--w->ref_cnt == 0)
	free_vid_win(wid);
}

/* Initialize video call media */
pj_status_t pjsua_vid_channel_init(pjsua_call_media *call_med)
{
    pjsua_acc *acc = &pjsua_var.acc[call_med->call->acc_id];

    call_med->strm.v.rdr_dev = acc->cfg.vid_rend_dev;
    call_med->strm.v.cap_dev = acc->cfg.vid_cap_dev;
    if (call_med->strm.v.rdr_dev == PJMEDIA_VID_DEFAULT_RENDER_DEV) {
	pjmedia_vid_dev_info info;
	pjmedia_vid_dev_get_info(call_med->strm.v.rdr_dev, &info);
	call_med->strm.v.rdr_dev = info.id;
    }
    if (call_med->strm.v.cap_dev == PJMEDIA_VID_DEFAULT_CAPTURE_DEV) {
	pjmedia_vid_dev_info info;
	pjmedia_vid_dev_get_info(call_med->strm.v.cap_dev, &info);
	call_med->strm.v.cap_dev = info.id;
    }

    return PJ_SUCCESS;
}

/* Internal function: update video channel after SDP negotiation */
pj_status_t pjsua_vid_channel_update(pjsua_call_media *call_med,
				     pj_pool_t *tmp_pool,
				     pjmedia_vid_stream_info *si,
				     const pjmedia_sdp_session *local_sdp,
				     const pjmedia_sdp_session *remote_sdp)
{
    pjsua_call *call = call_med->call;
    pjsua_acc  *acc  = &pjsua_var.acc[call->acc_id];
    pjmedia_port *media_port;
    pj_status_t status;
 
    PJ_UNUSED_ARG(tmp_pool);
    PJ_UNUSED_ARG(local_sdp);
    PJ_UNUSED_ARG(remote_sdp);

    PJ_LOG(4,(THIS_FILE, "Video channel update.."));
    pj_log_push_indent();

    si->rtcp_sdes_bye_disabled = PJ_TRUE;

    /* Check if no media is active */
    if (si->dir != PJMEDIA_DIR_NONE) {
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

	/* Set rate control config from account setting */
	si->rc_cfg = acc->cfg.vid_stream_rc_cfg;

#if defined(PJMEDIA_STREAM_ENABLE_KA) && PJMEDIA_STREAM_ENABLE_KA!=0
	/* Enable/disable stream keep-alive and NAT hole punch. */
	si->use_ka = pjsua_var.acc[call->acc_id].cfg.use_stream_ka;
#endif

	/* Try to get shared format ID between the capture device and 
	 * the encoder to avoid format conversion in the capture device.
	 */
	if (si->dir & PJMEDIA_DIR_ENCODING) {
	    pjmedia_vid_dev_info dev_info;
	    pjmedia_vid_codec_info *codec_info = &si->codec_info;
	    unsigned i, j;

	    status = pjmedia_vid_dev_get_info(call_med->strm.v.cap_dev,
					      &dev_info);
	    if (status != PJ_SUCCESS)
		goto on_error;

	    /* Find matched format ID */
	    for (i = 0; i < codec_info->dec_fmt_id_cnt; ++i) {
		for (j = 0; j < dev_info.fmt_cnt; ++j) {
		    if (codec_info->dec_fmt_id[i] == 
			(pjmedia_format_id)dev_info.fmt[j].id)
		    {
			/* Apply the matched format ID to the codec */
			si->codec_param->dec_fmt.id = 
						codec_info->dec_fmt_id[i];

			/* Force outer loop to break */
			i = codec_info->dec_fmt_id_cnt;
			break;
		    }
		}
	    }
	}

	/* Create session based on session info. */
	status = pjmedia_vid_stream_create(pjsua_var.med_endpt, NULL, si,
					   call_med->tp, NULL,
					   &call_med->strm.v.stream);
	if (status != PJ_SUCCESS)
	    goto on_error;

	/* Start stream */
	status = pjmedia_vid_stream_start(call_med->strm.v.stream);
	if (status != PJ_SUCCESS)
	    goto on_error;

	/* Setup decoding direction */
	if (si->dir & PJMEDIA_DIR_DECODING)
	{
	    pjsua_vid_win_id wid;
	    pjsua_vid_win *w;

	    PJ_LOG(4,(THIS_FILE, "Setting up RX.."));
	    pj_log_push_indent();

	    status = pjmedia_vid_stream_get_port(call_med->strm.v.stream,
						 PJMEDIA_DIR_DECODING,
						 &media_port);
	    if (status != PJ_SUCCESS) {
		pj_log_pop_indent();
		goto on_error;
	    }

	    /* Create stream video window */
	    status = create_vid_win(PJSUA_WND_TYPE_STREAM,
				    &media_port->info.fmt,
				    call_med->strm.v.rdr_dev,
				    //acc->cfg.vid_rend_dev,
				    PJSUA_INVALID_ID,
				    acc->cfg.vid_in_auto_show,
                                    acc->cfg.vid_wnd_flags,
				    &wid);
	    if (status != PJ_SUCCESS) {
		pj_log_pop_indent();
		goto on_error;
	    }

	    w = &pjsua_var.win[wid];

#if ENABLE_EVENT
	    /* Register to video events */
	    pjmedia_event_subscribe(NULL, &call_media_on_event,
                                    call_med, w->vp_rend);
#endif
	    
	    /* Connect renderer to stream */
	    status = pjmedia_vid_port_connect(w->vp_rend, media_port,
					      PJ_FALSE);
	    if (status != PJ_SUCCESS) {
		pj_log_pop_indent();
		goto on_error;
	    }

	    /* Start renderer */
	    status = pjmedia_vid_port_start(w->vp_rend);
	    if (status != PJ_SUCCESS) {
		pj_log_pop_indent();
		goto on_error;
	    }

	    /* Done */
	    inc_vid_win(wid);
	    call_med->strm.v.rdr_win_id = wid;
	    pj_log_pop_indent();
	}

	/* Setup encoding direction */
	if (si->dir & PJMEDIA_DIR_ENCODING && !call->local_hold)
	{
            pjsua_acc *acc = &pjsua_var.acc[call_med->call->acc_id];
	    pjsua_vid_win *w;
	    pjsua_vid_win_id wid;
	    pj_bool_t just_created = PJ_FALSE;

	    PJ_LOG(4,(THIS_FILE, "Setting up TX.."));
	    pj_log_push_indent();

	    status = pjmedia_vid_stream_get_port(call_med->strm.v.stream,
						 PJMEDIA_DIR_ENCODING,
						 &media_port);
	    if (status != PJ_SUCCESS) {
		pj_log_pop_indent();
		goto on_error;
	    }

	    /* Note: calling pjsua_vid_preview_get_win() even though
	     * create_vid_win() will automatically create the window
	     * if it doesn't exist, because create_vid_win() will modify
	     * existing window SHOW/HIDE value.
	     */
	    wid = vid_preview_get_win(call_med->strm.v.cap_dev, PJ_FALSE);
	    if (wid == PJSUA_INVALID_ID) {
		/* Create preview video window */
		status = create_vid_win(PJSUA_WND_TYPE_PREVIEW,
					&media_port->info.fmt,
					call_med->strm.v.rdr_dev,
					call_med->strm.v.cap_dev,
					//acc->cfg.vid_rend_dev,
					//acc->cfg.vid_cap_dev,
					PJSUA_HIDE_WINDOW,
                                        acc->cfg.vid_wnd_flags,
					&wid);
		if (status != PJ_SUCCESS) {
		pj_log_pop_indent();
		    return status;
		}
		just_created = PJ_TRUE;
	    }

	    w = &pjsua_var.win[wid];
#if ENABLE_EVENT
            pjmedia_event_subscribe(NULL, &call_media_on_event,
                                    call_med, w->vp_cap);
#endif
	    
	    /* Connect stream to capturer (via video window tee) */
	    status = pjmedia_vid_tee_add_dst_port2(w->tee, 0, media_port);
	    if (status != PJ_SUCCESS) {
		pj_log_pop_indent();
		goto on_error;
	    }

	    /* Start capturer */
	    if (just_created) {
		status = pjmedia_vid_port_start(w->vp_cap);
		if (status != PJ_SUCCESS) {
		    pj_log_pop_indent();
		    goto on_error;
		}
	    }

	    /* Done */
	    inc_vid_win(wid);
	    call_med->strm.v.cap_win_id = wid;
	    pj_log_pop_indent();
	}

    }

    if (!acc->cfg.vid_out_auto_transmit && call_med->strm.v.stream) {
	status = pjmedia_vid_stream_pause(call_med->strm.v.stream,
					  PJMEDIA_DIR_ENCODING);
	if (status != PJ_SUCCESS)
	    goto on_error;
    }

    pj_log_pop_indent();
    return PJ_SUCCESS;

on_error:
    pj_log_pop_indent();
    return status;
}


/* Internal function to stop video stream */
void pjsua_vid_stop_stream(pjsua_call_media *call_med)
{
    pjmedia_vid_stream *strm = call_med->strm.v.stream;
    pjmedia_rtcp_stat stat;

    pj_assert(call_med->type == PJMEDIA_TYPE_VIDEO);

    if (!strm)
	return;

    PJ_LOG(4,(THIS_FILE, "Stopping video stream.."));
    pj_log_push_indent();

    if (call_med->strm.v.cap_win_id != PJSUA_INVALID_ID) {
	pjmedia_port *media_port;
	pjsua_vid_win *w = &pjsua_var.win[call_med->strm.v.cap_win_id];
	pj_status_t status;

	/* Stop the capture before detaching stream and unsubscribing event */
	pjmedia_vid_port_stop(w->vp_cap);

	/* Disconnect video stream from capture device */
	status = pjmedia_vid_stream_get_port(call_med->strm.v.stream,
					     PJMEDIA_DIR_ENCODING,
					     &media_port);
	if (status == PJ_SUCCESS) {
	    pjmedia_vid_tee_remove_dst_port(w->tee, media_port);
	}

        /* Unsubscribe event */
	pjmedia_event_unsubscribe(NULL, &call_media_on_event, call_med,
                                  w->vp_cap);

	/* Re-start capture again, if it is used by other stream */
	if (w->ref_cnt > 1)
	    pjmedia_vid_port_start(w->vp_cap);

	dec_vid_win(call_med->strm.v.cap_win_id);
	call_med->strm.v.cap_win_id = PJSUA_INVALID_ID;
    }

    if (call_med->strm.v.rdr_win_id != PJSUA_INVALID_ID) {
	pjsua_vid_win *w = &pjsua_var.win[call_med->strm.v.rdr_win_id];

	/* Stop the render before unsubscribing event */
	pjmedia_vid_port_stop(w->vp_rend);
	pjmedia_event_unsubscribe(NULL, &call_media_on_event, call_med,
                                  w->vp_rend);

	dec_vid_win(call_med->strm.v.rdr_win_id);
	call_med->strm.v.rdr_win_id = PJSUA_INVALID_ID;
    }

    if ((call_med->dir & PJMEDIA_DIR_ENCODING) &&
	(pjmedia_vid_stream_get_stat(strm, &stat) == PJ_SUCCESS))
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

    pjmedia_vid_stream_destroy(strm);
    call_med->strm.v.stream = NULL;

    pj_log_pop_indent();
}

/*
 * Does it have built-in preview support.
 */
PJ_DEF(pj_bool_t) pjsua_vid_preview_has_native(pjmedia_vid_dev_index id)
{
    pjmedia_vid_dev_info vdi;

    return (pjmedia_vid_dev_get_info(id, &vdi)==PJ_SUCCESS) ?
	    ((vdi.caps & PJMEDIA_VID_DEV_CAP_INPUT_PREVIEW)!=0) : PJ_FALSE;
}

/*
 * Start video preview window for the specified capture device.
 */
PJ_DEF(pj_status_t) pjsua_vid_preview_start(pjmedia_vid_dev_index id,
                                            const pjsua_vid_preview_param *prm)
{
    pjsua_vid_win_id wid;
    pjsua_vid_win *w;
    pjmedia_vid_dev_index rend_id;
    pjsua_vid_preview_param default_param;
    pj_status_t status;

    if (!prm) {
	pjsua_vid_preview_param_default(&default_param);
	prm = &default_param;
    }

    PJ_LOG(4,(THIS_FILE, "Starting preview for cap_dev=%d, show=%d",
	      id, prm->show));
    pj_log_push_indent();

    PJSUA_LOCK();

    rend_id = prm->rend_id;

    status = create_vid_win(PJSUA_WND_TYPE_PREVIEW, NULL, rend_id, id,
			    prm->show, prm->wnd_flags, &wid);
    if (status != PJ_SUCCESS) {
	PJSUA_UNLOCK();
	pj_log_pop_indent();
	return status;
    }

    w = &pjsua_var.win[wid];
    if (w->preview_running) {
	PJSUA_UNLOCK();
	pj_log_pop_indent();
	return PJ_SUCCESS;
    }

    /* Start renderer, unless it's native preview */
    if (w->is_native && !pjmedia_vid_port_is_running(w->vp_cap)) {
	pjmedia_vid_dev_stream *cap_dev;
	pj_bool_t enabled = PJ_TRUE;

	cap_dev = pjmedia_vid_port_get_stream(w->vp_cap);
	status = pjmedia_vid_dev_stream_set_cap(
			cap_dev, PJMEDIA_VID_DEV_CAP_INPUT_PREVIEW,
			&enabled);
	if (status != PJ_SUCCESS) {
	    PJ_PERROR(1,(THIS_FILE, status,
			 "Error activating native preview, falling back "
			 "to software preview.."));
	    w->is_native = PJ_FALSE;
	}
    }

    if (!w->is_native && !pjmedia_vid_port_is_running(w->vp_rend)) {
	status = pjmedia_vid_port_start(w->vp_rend);
	if (status != PJ_SUCCESS) {
	    PJSUA_UNLOCK();
	    pj_log_pop_indent();
	    return status;
	}
    }

    /* Start capturer */
    if (!pjmedia_vid_port_is_running(w->vp_cap)) {
	status = pjmedia_vid_port_start(w->vp_cap);
	if (status != PJ_SUCCESS) {
	    PJSUA_UNLOCK();
	    pj_log_pop_indent();
	    return status;
	}
    }

    inc_vid_win(wid);
    w->preview_running = PJ_TRUE;

    PJSUA_UNLOCK();
    pj_log_pop_indent();
    return PJ_SUCCESS;
}

/*
 * Stop video preview.
 */
PJ_DEF(pj_status_t) pjsua_vid_preview_stop(pjmedia_vid_dev_index id)
{
    pjsua_vid_win_id wid = PJSUA_INVALID_ID;
    pjsua_vid_win *w;
    pj_status_t status;

    PJSUA_LOCK();
    wid = pjsua_vid_preview_get_win(id);
    if (wid == PJSUA_INVALID_ID) {
	PJSUA_UNLOCK();
	pj_log_pop_indent();
	return PJ_ENOTFOUND;
    }

    PJ_LOG(4,(THIS_FILE, "Stopping preview for cap_dev=%d", id));
    pj_log_push_indent();

    w = &pjsua_var.win[wid];
    if (w->preview_running) {
	if (w->is_native) {
	    pjmedia_vid_dev_stream *cap_dev;
	    pj_bool_t enabled = PJ_FALSE;

	    cap_dev = pjmedia_vid_port_get_stream(w->vp_cap);
	    status = pjmedia_vid_dev_stream_set_cap(
			    cap_dev, PJMEDIA_VID_DEV_CAP_INPUT_PREVIEW,
			    &enabled);
	} else {
	    status = pjmedia_vid_port_stop(w->vp_rend);
	}

	if (status != PJ_SUCCESS) {
	    PJ_PERROR(1,(THIS_FILE, status, "Error stopping %spreview",
		         (w->is_native ? "native " : "")));
	    PJSUA_UNLOCK();
	    pj_log_pop_indent();
	    return status;
	}

	dec_vid_win(wid);
	w->preview_running = PJ_FALSE;
    }

    PJSUA_UNLOCK();
    pj_log_pop_indent();

    return PJ_SUCCESS;
}


/*****************************************************************************
 * Window
 */


/*
 * Enumerates all video windows.
 */
PJ_DEF(pj_status_t) pjsua_vid_enum_wins( pjsua_vid_win_id wids[],
					 unsigned *count)
{
    unsigned i, cnt;

    cnt = 0;

    for (i=0; i<PJSUA_MAX_VID_WINS && cnt <*count; ++i) {
	pjsua_vid_win *w = &pjsua_var.win[i];
	if (w->type != PJSUA_WND_TYPE_NONE)
	    wids[cnt++] = i;
    }

    *count = cnt;

    return PJ_SUCCESS;
}


/*
 * Get window info.
 */
PJ_DEF(pj_status_t) pjsua_vid_win_get_info( pjsua_vid_win_id wid,
                                            pjsua_vid_win_info *wi)
{
    pjsua_vid_win *w;
    pjmedia_vid_dev_stream *s;
    pjmedia_vid_dev_param vparam;
    pj_status_t status;

    PJ_ASSERT_RETURN(wid >= 0 && wid < PJSUA_MAX_VID_WINS && wi, PJ_EINVAL);

    pj_bzero(wi, sizeof(*wi));

    PJSUA_LOCK();
    w = &pjsua_var.win[wid];

    wi->is_native = w->is_native;

    if (w->is_native) {
	pjmedia_vid_dev_stream *cap_strm;
	pjmedia_vid_dev_cap cap = PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW;

	cap_strm = pjmedia_vid_port_get_stream(w->vp_cap);
	if (!cap_strm) {
	    status = PJ_EINVAL;
	} else {
	    status = pjmedia_vid_dev_stream_get_cap(cap_strm, cap, &wi->hwnd);
	}

	PJSUA_UNLOCK();
	return status;
    }

    if (w->vp_rend == NULL) {
	PJSUA_UNLOCK();
	return PJ_EINVAL;
    }

    s = pjmedia_vid_port_get_stream(w->vp_rend);
    if (s == NULL) {
	PJSUA_UNLOCK();
	return PJ_EINVAL;
    }

    status = pjmedia_vid_dev_stream_get_param(s, &vparam);
    if (status != PJ_SUCCESS) {
	PJSUA_UNLOCK();
	return status;
    }

    wi->rdr_dev = vparam.rend_id;
    wi->hwnd = vparam.window;
    wi->show = !vparam.window_hide;
    wi->pos  = vparam.window_pos;
    wi->size = vparam.disp_size;

    PJSUA_UNLOCK();

    return PJ_SUCCESS;
}

/*
 * Show or hide window.
 */
PJ_DEF(pj_status_t) pjsua_vid_win_set_show( pjsua_vid_win_id wid,
                                            pj_bool_t show)
{
    pjsua_vid_win *w;
    pjmedia_vid_dev_stream *s;
    pj_bool_t hide;
    pj_status_t status;

    PJ_ASSERT_RETURN(wid >= 0 && wid < PJSUA_MAX_VID_WINS, PJ_EINVAL);

    PJSUA_LOCK();
    w = &pjsua_var.win[wid];
    if (w->vp_rend == NULL) {
	/* Native window */
	PJSUA_UNLOCK();
	return PJ_EINVAL;
    }

    s = pjmedia_vid_port_get_stream(w->vp_rend);
    if (s == NULL) {
	PJSUA_UNLOCK();
	return PJ_EINVAL;
    }

    /* Make sure that renderer gets started before shown up */
    if (show && !pjmedia_vid_port_is_running(w->vp_rend))
	status = pjmedia_vid_port_start(w->vp_rend);

    hide = !show;
    status = pjmedia_vid_dev_stream_set_cap(s,
			    PJMEDIA_VID_DEV_CAP_OUTPUT_HIDE, &hide);

    PJSUA_UNLOCK();

    return status;
}

/*
 * Set video window position.
 */
PJ_DEF(pj_status_t) pjsua_vid_win_set_pos( pjsua_vid_win_id wid,
                                           const pjmedia_coord *pos)
{
    pjsua_vid_win *w;
    pjmedia_vid_dev_stream *s;
    pj_status_t status;

    PJ_ASSERT_RETURN(wid >= 0 && wid < PJSUA_MAX_VID_WINS && pos, PJ_EINVAL);

    PJSUA_LOCK();
    w = &pjsua_var.win[wid];
    if (w->vp_rend == NULL) {
	/* Native window */
	PJSUA_UNLOCK();
	return PJ_EINVAL;
    }

    s = pjmedia_vid_port_get_stream(w->vp_rend);
    if (s == NULL) {
	PJSUA_UNLOCK();
	return PJ_EINVAL;
    }

    status = pjmedia_vid_dev_stream_set_cap(s,
			    PJMEDIA_VID_DEV_CAP_OUTPUT_POSITION, pos);

    PJSUA_UNLOCK();

    return status;
}

/*
 * Resize window.
 */
PJ_DEF(pj_status_t) pjsua_vid_win_set_size( pjsua_vid_win_id wid,
                                            const pjmedia_rect_size *size)
{
    pjsua_vid_win *w;
    pjmedia_vid_dev_stream *s;
    pj_status_t status;

    PJ_ASSERT_RETURN(wid >= 0 && wid < PJSUA_MAX_VID_WINS && size, PJ_EINVAL);

    PJSUA_LOCK();
    w = &pjsua_var.win[wid];
    if (w->vp_rend == NULL) {
	/* Native window */
	PJSUA_UNLOCK();
	return PJ_EINVAL;
    }

    s = pjmedia_vid_port_get_stream(w->vp_rend);
    if (s == NULL) {
	PJSUA_UNLOCK();
	return PJ_EINVAL;
    }

    status = pjmedia_vid_dev_stream_set_cap(s,
			    PJMEDIA_VID_DEV_CAP_OUTPUT_RESIZE, size);

    PJSUA_UNLOCK();

    return status;
}

/*
 * Set video orientation.
 */
PJ_DEF(pj_status_t) pjsua_vid_win_rotate( pjsua_vid_win_id wid,
                                          int angle)
{
    pjsua_vid_win *w;
    pjmedia_vid_dev_stream *s;
    pjmedia_orient orient;
    pj_status_t status;

    PJ_ASSERT_RETURN(wid >= 0 && wid < PJSUA_MAX_VID_WINS, PJ_EINVAL);
    PJ_ASSERT_RETURN((angle % 90) == 0, PJ_EINVAL);

    /* Normalize angle, so it must be 0, 90, 180, or 270. */
    angle %= 360;
    if (angle < 0)
	angle += 360;

    /* Convert angle to pjmedia_orient */
    switch(angle) {
	case 0:
	    /* No rotation */
	    return PJ_SUCCESS;
	case 90:
	    orient = PJMEDIA_ORIENT_ROTATE_90DEG;
	    break;
	case 180:
	    orient = PJMEDIA_ORIENT_ROTATE_180DEG;
	    break;
	case 270:
	    orient = PJMEDIA_ORIENT_ROTATE_270DEG;
	    break;
	default:
	    pj_assert(!"Angle must have been validated");
	    return PJ_EBUG;
    }

    PJSUA_LOCK();
    w = &pjsua_var.win[wid];
    if (w->vp_rend == NULL) {
	/* Native window */
	PJSUA_UNLOCK();
	return PJ_EINVAL;
    }

    s = pjmedia_vid_port_get_stream(w->vp_rend);
    if (s == NULL) {
	PJSUA_UNLOCK();
	return PJ_EINVAL;
    }

    status = pjmedia_vid_dev_stream_set_cap(s,
			    PJMEDIA_VID_DEV_CAP_ORIENTATION, &orient);

    PJSUA_UNLOCK();

    return status;
}


static void call_get_vid_strm_info(pjsua_call *call,
				   int *first_active,
				   int *first_inactive,
				   unsigned *active_cnt,
				   unsigned *cnt)
{
    unsigned i, var_cnt = 0;

    if (first_active && ++var_cnt)
	*first_active = -1;
    if (first_inactive && ++var_cnt)
	*first_inactive = -1;
    if (active_cnt && ++var_cnt)
	*active_cnt = 0;
    if (cnt && ++var_cnt)
	*cnt = 0;

    for (i = 0; i < call->med_cnt && var_cnt; ++i) {
	if (call->media[i].type == PJMEDIA_TYPE_VIDEO) {
	    if (call->media[i].dir != PJMEDIA_DIR_NONE)
	    {
		if (first_active && *first_active == -1) {
		    *first_active = i;
		    --var_cnt;
		}
		if (active_cnt)
		    ++(*active_cnt);
	    } else if (first_inactive && *first_inactive == -1) {
		*first_inactive = i;
		--var_cnt;
	    }
	    if (cnt)
		++(*cnt);
	}
    }
}


/* Send SDP reoffer. */
static pj_status_t call_reoffer_sdp(pjsua_call_id call_id,
				    const pjmedia_sdp_session *sdp)
{
    pjsua_call *call;
    pjsip_tx_data *tdata;
    pjsip_dialog *dlg;
    pj_status_t status;

    status = acquire_call("call_reoffer_sdp()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	return status;

    if (call->inv->state != PJSIP_INV_STATE_CONFIRMED) {
	PJ_LOG(3,(THIS_FILE, "Can not re-INVITE call that is not confirmed"));
	pjsip_dlg_dec_lock(dlg);
	return PJSIP_ESESSIONSTATE;
    }

    /* Create re-INVITE with new offer */
    status = pjsip_inv_reinvite( call->inv, NULL, sdp, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create re-INVITE", status);
	pjsip_dlg_dec_lock(dlg);
	return status;
    }

    /* Send the request */
    status = pjsip_inv_send_msg( call->inv, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send re-INVITE", status);
	pjsip_dlg_dec_lock(dlg);
	return status;
    }

    pjsip_dlg_dec_lock(dlg);

    return PJ_SUCCESS;
}

/* Add a new video stream into a call */
static pj_status_t call_add_video(pjsua_call *call,
				  pjmedia_vid_dev_index cap_dev,
				  pjmedia_dir dir)
{
    pj_pool_t *pool = call->inv->pool_prov;
    pjsua_acc_config *acc_cfg = &pjsua_var.acc[call->acc_id].cfg;
    pjsua_call_media *call_med;
    const pjmedia_sdp_session *current_sdp;
    pjmedia_sdp_session *sdp;
    pjmedia_sdp_media *sdp_m;
    pjmedia_transport_info tpinfo;
    pj_status_t status;

    /* Verify media slot availability */
    if (call->med_cnt == PJSUA_MAX_CALL_MEDIA)
	return PJ_ETOOMANY;

    /* Get active local SDP and clone it */
    status = pjmedia_sdp_neg_get_active_local(call->inv->neg, &current_sdp);
    if (status != PJ_SUCCESS)
	return status;

    sdp = pjmedia_sdp_session_clone(call->inv->pool_prov, current_sdp);

    /* Clean up provisional media before using it */
    pjsua_media_prov_clean_up(call->index);

    /* Update provisional media from call media */
    call->med_prov_cnt = call->med_cnt;
    pj_memcpy(call->media_prov, call->media,
	      sizeof(call->media[0]) * call->med_cnt);

    /* Initialize call media */
    call_med = &call->media_prov[call->med_prov_cnt++];
    status = pjsua_call_media_init(call_med, PJMEDIA_TYPE_VIDEO,
				   &acc_cfg->rtp_cfg, call->secure_level,
				   NULL, PJ_FALSE, NULL);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Override default capture device setting */
    call_med->strm.v.cap_dev = cap_dev;

    /* Init transport media */
    status = pjmedia_transport_media_create(call_med->tp, pool, 0,
					    NULL, call_med->idx);
    if (status != PJ_SUCCESS)
	goto on_error;

    pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_INIT);

    /* Get transport address info */
    pjmedia_transport_info_init(&tpinfo);
    pjmedia_transport_get_info(call_med->tp, &tpinfo);

    /* Create SDP media line */
    status = pjmedia_endpt_create_video_sdp(pjsua_var.med_endpt, pool,
					    &tpinfo.sock_info, 0, &sdp_m);
    if (status != PJ_SUCCESS)
	goto on_error;

    sdp->media[sdp->media_count++] = sdp_m;

    /* Update media direction, if it is not 'sendrecv' */
    if (dir != PJMEDIA_DIR_ENCODING_DECODING) {
	pjmedia_sdp_attr *a;

	/* Remove sendrecv direction attribute, if any */
	pjmedia_sdp_media_remove_all_attr(sdp_m, "sendrecv");

	if (dir == PJMEDIA_DIR_ENCODING)
	    a = pjmedia_sdp_attr_create(pool, "sendonly", NULL);
	else if (dir == PJMEDIA_DIR_DECODING)
	    a = pjmedia_sdp_attr_create(pool, "recvonly", NULL);
	else
	    a = pjmedia_sdp_attr_create(pool, "inactive", NULL);

	pjmedia_sdp_media_add_attr(sdp_m, a);
    }

    /* Update SDP media line by media transport */
    status = pjmedia_transport_encode_sdp(call_med->tp, pool,
					  sdp, NULL, call_med->idx);
    if (status != PJ_SUCCESS)
	goto on_error;

    status = call_reoffer_sdp(call->index, sdp);
    if (status != PJ_SUCCESS)
	goto on_error;

    call->opt.vid_cnt++;

    return PJ_SUCCESS;

on_error:
    if (call_med->tp) {
	pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_NULL);
	pjmedia_transport_close(call_med->tp);
	call_med->tp = call_med->tp_orig = NULL;
    }

    return status;
}


/* Modify a video stream from a call, i.e: update direction,
 * remove/disable.
 */
static pj_status_t call_modify_video(pjsua_call *call,
				     int med_idx,
				     pjmedia_dir dir,
				     pj_bool_t remove)
{
    pjsua_call_media *call_med;
    const pjmedia_sdp_session *current_sdp;
    pjmedia_sdp_session *sdp;
    pj_status_t status;

    /* Verify and normalize media index */
    if (med_idx == -1) {
	int first_active;
	
	call_get_vid_strm_info(call, &first_active, NULL, NULL, NULL);
	if (first_active == -1)
	    return PJ_ENOTFOUND;

	med_idx = first_active;
    }

    /* Clean up provisional media before using it */
    pjsua_media_prov_clean_up(call->index);

    /* Update provisional media from call media */
    call->med_prov_cnt = call->med_cnt;
    pj_memcpy(call->media_prov, call->media,
	      sizeof(call->media[0]) * call->med_cnt);

    call_med = &call->media_prov[med_idx];

    /* Verify if the stream media type is video */
    if (call_med->type != PJMEDIA_TYPE_VIDEO)
	return PJ_EINVAL;

    /* Verify if the stream dir is not changed */
    if ((!remove && call_med->dir == dir) ||
	( remove && (call_med->tp_st == PJSUA_MED_TP_DISABLED ||
		     call_med->tp == NULL)))
    {
	return PJ_SUCCESS;
    }

    /* Get active local SDP and clone it */
    status = pjmedia_sdp_neg_get_active_local(call->inv->neg, &current_sdp);
    if (status != PJ_SUCCESS)
	return status;

    sdp = pjmedia_sdp_session_clone(call->inv->pool_prov, current_sdp);

    pj_assert(med_idx < (int)sdp->media_count);

    if (!remove) {
	pjsua_acc_config *acc_cfg = &pjsua_var.acc[call->acc_id].cfg;
	pj_pool_t *pool = call->inv->pool_prov;
	pjmedia_sdp_media *sdp_m;

	/* Enabling video */
	if (call_med->dir == PJMEDIA_DIR_NONE) {
	    unsigned i, vid_cnt = 0;

	    /* Check if vid_cnt in call option needs to be increased */
	    for (i = 0; i < call->med_cnt; ++i) {
		if (call->media[i].type == PJMEDIA_TYPE_VIDEO &&
		    call->media[i].dir != PJMEDIA_DIR_NONE)
		{
		    ++vid_cnt;
		}
	    }
	    if (call->opt.vid_cnt <= vid_cnt)
		call->opt.vid_cnt++;
	}

	status = pjsua_call_media_init(call_med, PJMEDIA_TYPE_VIDEO,
				       &acc_cfg->rtp_cfg, call->secure_level,
				       NULL, PJ_FALSE, NULL);
	if (status != PJ_SUCCESS)
	    goto on_error;

	/* Init transport media */
	if (call_med->tp && call_med->tp_st == PJSUA_MED_TP_IDLE) {
	    status = pjmedia_transport_media_create(call_med->tp, pool, 0,
						    NULL, call_med->idx);
	    if (status != PJ_SUCCESS)
		goto on_error;
	}

	sdp_m = sdp->media[med_idx];

	/* Create new SDP media line if the stream is disabled */
	if (sdp->media[med_idx]->desc.port == 0) {
	    pjmedia_transport_info tpinfo;

	    /* Get transport address info */
	    pjmedia_transport_info_init(&tpinfo);
	    pjmedia_transport_get_info(call_med->tp, &tpinfo);

	    status = pjmedia_endpt_create_video_sdp(pjsua_var.med_endpt, pool,
						    &tpinfo.sock_info, 0, &sdp_m);
	    if (status != PJ_SUCCESS)
		goto on_error;
	}

	{
	    pjmedia_sdp_attr *a;

	    /* Remove any direction attributes */
	    pjmedia_sdp_media_remove_all_attr(sdp_m, "sendrecv");
	    pjmedia_sdp_media_remove_all_attr(sdp_m, "sendonly");
	    pjmedia_sdp_media_remove_all_attr(sdp_m, "recvonly");
	    pjmedia_sdp_media_remove_all_attr(sdp_m, "inactive");

	    /* Update media direction */
	    if (dir == PJMEDIA_DIR_ENCODING_DECODING)
		a = pjmedia_sdp_attr_create(pool, "sendrecv", NULL);
	    else if (dir == PJMEDIA_DIR_ENCODING)
		a = pjmedia_sdp_attr_create(pool, "sendonly", NULL);
	    else if (dir == PJMEDIA_DIR_DECODING)
		a = pjmedia_sdp_attr_create(pool, "recvonly", NULL);
	    else
		a = pjmedia_sdp_attr_create(pool, "inactive", NULL);

	    pjmedia_sdp_media_add_attr(sdp_m, a);
	}

	sdp->media[med_idx] = sdp_m;

	/* Update SDP media line by media transport */
	status = pjmedia_transport_encode_sdp(call_med->tp, pool,
					      sdp, NULL, call_med->idx);
	if (status != PJ_SUCCESS)
	    goto on_error;

on_error:
	if (status != PJ_SUCCESS) {
	    pjsua_media_prov_clean_up(call->index);
	    return status;
	}
    
    } else {

	pj_pool_t *pool = call->inv->pool_prov;

	/* Mark media transport to disabled */
	// Don't close this here, as SDP negotiation has not been
	// done and stream may be still active.
	pjsua_set_media_tp_state(call_med, PJSUA_MED_TP_DISABLED);

	/* Deactivate the stream */
	pjmedia_sdp_media_deactivate(pool, sdp->media[med_idx]);

	call->opt.vid_cnt--;
    }

    status = call_reoffer_sdp(call->index, sdp);
    if (status != PJ_SUCCESS)
	return status;

    return PJ_SUCCESS;
}


/* Change capture device of a video stream in a call */
static pj_status_t call_change_cap_dev(pjsua_call *call,
				       int med_idx,
				       pjmedia_vid_dev_index cap_dev)
{
    pjsua_call_media *call_med;
    pjmedia_vid_dev_stream *old_dev;
    pjmedia_vid_dev_switch_param switch_prm;
    pjmedia_vid_dev_info info;
    pjsua_vid_win *w, *new_w = NULL;
    pjsua_vid_win_id wid, new_wid;
    pjmedia_port *media_port;
    pj_status_t status;

    /* Verify and normalize media index */
    if (med_idx == -1) {
	int first_active;
	
	call_get_vid_strm_info(call, &first_active, NULL, NULL, NULL);
	if (first_active == -1)
	    return PJ_ENOTFOUND;

	med_idx = first_active;
    }

    call_med = &call->media[med_idx];

    /* Verify if the stream media type is video */
    if (call_med->type != PJMEDIA_TYPE_VIDEO)
	return PJ_EINVAL;

    /* Verify the capture device */
    status = pjmedia_vid_dev_get_info(cap_dev, &info);
    if (status != PJ_SUCCESS || info.dir != PJMEDIA_DIR_CAPTURE)
	return PJ_EINVAL;

    /* The specified capture device is being used already */
    if (call_med->strm.v.cap_dev == cap_dev)
	return PJ_SUCCESS;

    /* == Apply the new capture device == */

    wid = call_med->strm.v.cap_win_id;
    w = &pjsua_var.win[wid];
    pj_assert(w->type == PJSUA_WND_TYPE_PREVIEW && w->vp_cap);

    /* If the old device supports fast switching, then that's excellent! */
    old_dev = pjmedia_vid_port_get_stream(w->vp_cap);
    pjmedia_vid_dev_switch_param_default(&switch_prm);
    switch_prm.target_id = cap_dev;
    status = pjmedia_vid_dev_stream_set_cap(old_dev,
                                            PJMEDIA_VID_DEV_CAP_SWITCH,
                                            &switch_prm);
    if (status == PJ_SUCCESS) {
	w->preview_cap_id = cap_dev;
	call_med->strm.v.cap_dev = cap_dev;
	return PJ_SUCCESS;
    }

    /* No it doesn't support fast switching. Do slow switching then.. */
    status = pjmedia_vid_stream_get_port(call_med->strm.v.stream,
					 PJMEDIA_DIR_ENCODING, &media_port);
    if (status != PJ_SUCCESS)
	return status;

    pjmedia_event_unsubscribe(NULL, &call_media_on_event, call_med,
                              w->vp_cap);
    
    /* temporarily disconnect while we operate on the tee. */
    pjmedia_vid_port_disconnect(w->vp_cap);

    /* = Detach stream port from the old capture device's tee = */
    status = pjmedia_vid_tee_remove_dst_port(w->tee, media_port);
    if (status != PJ_SUCCESS) {
	/* Something wrong, assume that media_port has been removed
	 * and continue.
	 */
	PJ_PERROR(4,(THIS_FILE, status,
		     "Warning: call %d: unable to remove video from tee",
		     call->index));
    }

    /* Reconnect again immediately. We're done with w->tee */
    pjmedia_vid_port_connect(w->vp_cap, w->tee, PJ_FALSE);

    /* = Attach stream port to the new capture device = */

    /* Note: calling pjsua_vid_preview_get_win() even though
     * create_vid_win() will automatically create the window
     * if it doesn't exist, because create_vid_win() will modify
     * existing window SHOW/HIDE value.
     */
    new_wid = vid_preview_get_win(cap_dev, PJ_FALSE);
    if (new_wid == PJSUA_INVALID_ID) {
        pjsua_acc *acc = &pjsua_var.acc[call_med->call->acc_id];

	/* Create preview video window */
	status = create_vid_win(PJSUA_WND_TYPE_PREVIEW,
				&media_port->info.fmt,
				call_med->strm.v.rdr_dev,
				cap_dev,
				PJSUA_HIDE_WINDOW,
				acc->cfg.vid_wnd_flags,
                                &new_wid);
	if (status != PJ_SUCCESS)
	    goto on_error;
    }

    inc_vid_win(new_wid);
    new_w = &pjsua_var.win[new_wid];
    
    /* Connect stream to capturer (via video window tee) */
    status = pjmedia_vid_tee_add_dst_port2(new_w->tee, 0, media_port);
    if (status != PJ_SUCCESS)
	goto on_error;

    if (w->vp_rend) {
	/* Start renderer */
	status = pjmedia_vid_port_start(new_w->vp_rend);
	if (status != PJ_SUCCESS)
	    goto on_error;
    }

#if ENABLE_EVENT
    pjmedia_event_subscribe(NULL, &call_media_on_event,
                            call_med, new_w->vp_cap);
#endif

    /* Start capturer */
    if (!pjmedia_vid_port_is_running(new_w->vp_cap)) {
	status = pjmedia_vid_port_start(new_w->vp_cap);
	if (status != PJ_SUCCESS)
	    goto on_error;
    }

    /* Finally */
    call_med->strm.v.cap_dev = cap_dev;
    call_med->strm.v.cap_win_id = new_wid;
    dec_vid_win(wid);

    return PJ_SUCCESS;

on_error:
    PJ_PERROR(4,(THIS_FILE, status,
	         "Call %d: error changing capture device to %d",
	         call->index, cap_dev));

    if (new_w) {
	/* Unsubscribe, just in case */
        pjmedia_event_unsubscribe(NULL, &call_media_on_event, call_med,
                                  new_w->vp_cap);
	/* Disconnect media port from the new capturer */
	pjmedia_vid_tee_remove_dst_port(new_w->tee, media_port);
	/* Release the new capturer */
	dec_vid_win(new_wid);
    }

    /* Revert back to the old capturer */
    pjmedia_vid_port_disconnect(w->vp_cap);
    status = pjmedia_vid_tee_add_dst_port2(w->tee, 0, media_port);
    pjmedia_vid_port_connect(w->vp_cap, w->tee, PJ_FALSE);
    if (status != PJ_SUCCESS)
	return status;

#if ENABLE_EVENT
    /* Resubscribe */
    pjmedia_event_subscribe(NULL, &call_media_on_event,
                            call_med, w->vp_cap);
#endif

    return status;
}


/* Start/stop transmitting video stream in a call */
static pj_status_t call_set_tx_video(pjsua_call *call,
				     int med_idx,
				     pj_bool_t enable)
{
    pjsua_call_media *call_med;
    pj_status_t status;

    /* Verify and normalize media index */
    if (med_idx == -1) {
	int first_active;
	
	call_get_vid_strm_info(call, &first_active, NULL, NULL, NULL);
	if (first_active == -1)
	    return PJ_ENOTFOUND;

	med_idx = first_active;
    }

    call_med = &call->media[med_idx];

    /* Verify if the stream is transmitting video */
    if (call_med->type != PJMEDIA_TYPE_VIDEO || 
	(enable && (call_med->dir & PJMEDIA_DIR_ENCODING) == 0))
    {
	return PJ_EINVAL;
    }

    if (enable) {
	/* Start stream in encoding direction */
	status = pjmedia_vid_stream_resume(call_med->strm.v.stream,
					   PJMEDIA_DIR_ENCODING);
    } else {
	/* Pause stream in encoding direction */
	status = pjmedia_vid_stream_pause( call_med->strm.v.stream,
					   PJMEDIA_DIR_ENCODING);
    }

    return status;
}


static pj_status_t call_send_vid_keyframe(pjsua_call *call,
					  int med_idx)
{
    pjsua_call_media *call_med;

    /* Verify and normalize media index */
    if (med_idx == -1) {
	int first_active;
	
	call_get_vid_strm_info(call, &first_active, NULL, NULL, NULL);
	if (first_active == -1)
	    return PJ_ENOTFOUND;

	med_idx = first_active;
    }

    call_med = &call->media[med_idx];

    /* Verify media type and stream instance. */
    if (call_med->type != PJMEDIA_TYPE_VIDEO || !call_med->strm.v.stream)
	return PJ_EINVAL;

    return pjmedia_vid_stream_send_keyframe(call_med->strm.v.stream);
}


/*
 * Start, stop, and/or manipulate video transmission for the specified call.
 */
PJ_DEF(pj_status_t) pjsua_call_set_vid_strm (
				pjsua_call_id call_id,
				pjsua_call_vid_strm_op op,
				const pjsua_call_vid_strm_op_param *param)
{
    pjsua_call *call;
    pjsua_call_vid_strm_op_param param_;
    pj_status_t status;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(op != PJSUA_CALL_VID_STRM_NO_OP, PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Call %d: set video stream, op=%d",
	      call_id, op));
    pj_log_push_indent();

    PJSUA_LOCK();

    call = &pjsua_var.calls[call_id];

    if (param) {
	param_ = *param;
    } else {
	pjsua_call_vid_strm_op_param_default(&param_);
    }

    /* If set to PJMEDIA_VID_DEFAULT_CAPTURE_DEV, replace it with
     * account default video capture device.
     */
    if (param_.cap_dev == PJMEDIA_VID_DEFAULT_CAPTURE_DEV) {
	pjsua_acc_config *acc_cfg = &pjsua_var.acc[call->acc_id].cfg;
	param_.cap_dev = acc_cfg->vid_cap_dev;
	
	/* If the account default video capture device is
	 * PJMEDIA_VID_DEFAULT_CAPTURE_DEV, replace it with
	 * global default video capture device.
	 */
	if (param_.cap_dev == PJMEDIA_VID_DEFAULT_CAPTURE_DEV) {
	    pjmedia_vid_dev_info info;
	    pjmedia_vid_dev_get_info(param_.cap_dev, &info);
	    pj_assert(info.dir == PJMEDIA_DIR_CAPTURE);
	    param_.cap_dev = info.id;
	}
    }

    switch (op) {
    case PJSUA_CALL_VID_STRM_ADD:
	status = call_add_video(call, param_.cap_dev, param_.dir);
	break;
    case PJSUA_CALL_VID_STRM_REMOVE:
	status = call_modify_video(call, param_.med_idx, PJMEDIA_DIR_NONE,
				   PJ_TRUE);
	break;
    case PJSUA_CALL_VID_STRM_CHANGE_DIR:
	status = call_modify_video(call, param_.med_idx, param_.dir, PJ_FALSE);
	break;
    case PJSUA_CALL_VID_STRM_CHANGE_CAP_DEV:
	status = call_change_cap_dev(call, param_.med_idx, param_.cap_dev);
	break;
    case PJSUA_CALL_VID_STRM_START_TRANSMIT:
	status = call_set_tx_video(call, param_.med_idx, PJ_TRUE);
	break;
    case PJSUA_CALL_VID_STRM_STOP_TRANSMIT:
	status = call_set_tx_video(call, param_.med_idx, PJ_FALSE);
	break;
    case PJSUA_CALL_VID_STRM_SEND_KEYFRAME:
	status = call_send_vid_keyframe(call, param_.med_idx);
	break;
    default:
	status = PJ_EINVALIDOP;
	break;
    }

    PJSUA_UNLOCK();
    pj_log_pop_indent();

    return status;
}


/*
 * Get the media stream index of the default video stream in the call.
 */
PJ_DEF(int) pjsua_call_get_vid_stream_idx(pjsua_call_id call_id)
{
    pjsua_call *call;
    int first_active, first_inactive;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    PJSUA_LOCK();
    call = &pjsua_var.calls[call_id];
    call_get_vid_strm_info(call, &first_active, &first_inactive, NULL, NULL);
    PJSUA_UNLOCK();

    if (first_active == -1)
	return first_inactive;

    return first_active;
}


/*
 * Determine if video stream for the specified call is currently running
 * for the specified direction.
 */
PJ_DEF(pj_bool_t) pjsua_call_vid_stream_is_running( pjsua_call_id call_id,
                                                    int med_idx,
                                                    pjmedia_dir dir)
{
    pjsua_call *call;
    pjsua_call_media *call_med;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    /* Verify and normalize media index */
    if (med_idx == -1) {
	med_idx = pjsua_call_get_vid_stream_idx(call_id);
    }

    call = &pjsua_var.calls[call_id];
    PJ_ASSERT_RETURN(med_idx >= 0 && med_idx < (int)call->med_cnt, PJ_EINVAL);

    call_med = &call->media[med_idx];

    /* Verify if the stream is transmitting video */
    if (call_med->type != PJMEDIA_TYPE_VIDEO || (call_med->dir & dir) == 0 ||
	!call_med->strm.v.stream)
    {
	return PJ_FALSE;
    }

    return pjmedia_vid_stream_is_running(call_med->strm.v.stream, dir);
}

#endif /* PJSUA_HAS_VIDEO */

#endif /* PJSUA_MEDIA_HAS_PJMEDIA */
