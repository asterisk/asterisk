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
#  error The PJSUA_MEDIA_HAS_PJMEDIA should be declared as zero
#endif

#if PJSUA_HAS_VIDEO

#define THIS_FILE		"alt_pjsua_vid.c"
#define UNIMPLEMENTED(func)	PJ_LOG(2,(THIS_FILE, "*** Call to unimplemented function %s ***", #func));

/*****************************************************************************
 * Our video codec descriptors
 */
struct alt_codec_desc
{
    /* Predefined info */
    pjmedia_vid_codec_info       info;
    pjmedia_format_id		 base_fmt_id;
    pj_uint32_t			 avg_bps;
    pj_uint32_t			 max_bps;
    pjmedia_codec_fmtp		 dec_fmtp;
} alt_vid_codecs[] =
{
    /* H.263+ */
    {
	{PJMEDIA_FORMAT_H263P, PJMEDIA_RTP_PT_H263P, {"H263-1998",9},
	 {"H.263 codec", 11}, 90000, PJMEDIA_DIR_ENCODING_DECODING,
	 0, {PJMEDIA_FORMAT_RGB24}, PJMEDIA_VID_PACKING_PACKETS
	},
	PJMEDIA_FORMAT_H263,	256000,    512000,
	{2, { {{"CIF",3},   {"1",1}},
	      {{"QCIF",4},  {"1",1}}, } },
    }
};

static const struct alt_codec_desc* find_codec_desc_by_info(const pjmedia_vid_codec_info *info)
{
    unsigned i;
    for (i=0; i<PJ_ARRAY_SIZE(alt_vid_codecs); ++i) {
	struct alt_codec_desc *desc = &alt_vid_codecs[i];
	if ((desc->info.fmt_id == info->fmt_id) &&
            ((desc->info.dir & info->dir) == info->dir) &&
	    (desc->info.pt == info->pt) &&
	    (desc->info.packings & info->packings))
        {
            return desc;
        }
    }

    return NULL;
}

static pj_status_t alt_vid_codec_test_alloc( pjmedia_vid_codec_factory *factory,
                                             const pjmedia_vid_codec_info *id )
{
    const struct alt_codec_desc *desc = find_codec_desc_by_info(id);
    return desc? PJ_SUCCESS : PJMEDIA_CODEC_EUNSUP;
}

static pj_status_t alt_vid_codec_default_attr( pjmedia_vid_codec_factory *factory,
                                               const pjmedia_vid_codec_info *info,
                                               pjmedia_vid_codec_param *attr )
{
    const struct alt_codec_desc *desc = find_codec_desc_by_info(info);
    unsigned i;

    if (!desc)
        return PJMEDIA_CODEC_EUNSUP;

    pj_bzero(attr, sizeof(pjmedia_vid_codec_param));

    /* Scan the requested packings and use the lowest number */
    attr->packing = 0;
    for (i=0; i<15; ++i) {
	unsigned packing = (1 << i);
	if ((desc->info.packings & info->packings) & packing) {
	    attr->packing = (pjmedia_vid_packing)packing;
	    break;
	}
    }
    if (attr->packing == 0) {
	/* No supported packing in info */
	return PJMEDIA_CODEC_EUNSUP;
    }

    /* Direction */
    attr->dir = desc->info.dir;

    /* Encoded format */
    pjmedia_format_init_video(&attr->enc_fmt, desc->info.fmt_id,
                              720, 480, 30000, 1001);

    /* Decoded format */
    pjmedia_format_init_video(&attr->dec_fmt, desc->info.dec_fmt_id[0],
                              //352, 288, 30000, 1001);
                              720, 576, 30000, 1001);

    /* Decoding fmtp */
    attr->dec_fmtp = desc->dec_fmtp;

    /* Bitrate */
    attr->enc_fmt.det.vid.avg_bps = desc->avg_bps;
    attr->enc_fmt.det.vid.max_bps = desc->max_bps;

    /* MTU */
    attr->enc_mtu = PJMEDIA_MAX_MTU;

    return PJ_SUCCESS;
}


static pj_status_t alt_vid_codec_enum_codecs( pjmedia_vid_codec_factory *factory,
					      unsigned *count,
					      pjmedia_vid_codec_info codecs[])
{
    unsigned i, max_cnt;

    PJ_ASSERT_RETURN(codecs && *count > 0, PJ_EINVAL);

    max_cnt = PJ_MIN(*count, PJ_ARRAY_SIZE(alt_vid_codecs));
    *count = 0;

    for (i=0; i<max_cnt; ++i) {
	pj_memcpy(&codecs[*count], &alt_vid_codecs[i].info,
		  sizeof(pjmedia_vid_codec_info));
	(*count)++;
    }

    return PJ_SUCCESS;
}


static pj_status_t alt_vid_codec_alloc_codec( pjmedia_vid_codec_factory *factory,
					      const pjmedia_vid_codec_info *info,
					      pjmedia_vid_codec **p_codec)
{
    /* This will never get called since we won't be using this codec */
    UNIMPLEMENTED(alt_vid_codec_alloc_codec)
    return PJ_ENOTSUP;
}


static pj_status_t alt_vid_codec_dealloc_codec( pjmedia_vid_codec_factory *factory,
                                                pjmedia_vid_codec *codec )
{
    /* This will never get called since we won't be using this codec */
    UNIMPLEMENTED(alt_vid_codec_dealloc_codec)
    return PJ_ENOTSUP;
}

static pjmedia_vid_codec_factory_op alt_vid_codec_factory_op =
{
    &alt_vid_codec_test_alloc,
    &alt_vid_codec_default_attr,
    &alt_vid_codec_enum_codecs,
    &alt_vid_codec_alloc_codec,
    &alt_vid_codec_dealloc_codec
};

static struct alt_vid_codec_factory {
    pjmedia_vid_codec_factory    base;
} alt_vid_codec_factory;

/*****************************************************************************
 * Video API implementation
 */

/* Initialize the video library */
pj_status_t pjsua_vid_subsys_init(void)
{
    pjmedia_vid_codec_mgr *mgr;
    pj_status_t status;

    /* Format manager singleton is needed */
    status = pjmedia_video_format_mgr_create(pjsua_var.pool, 64, 0, NULL);
    if (status != PJ_SUCCESS) {
	PJ_PERROR(1,(THIS_FILE, status,
		     "Error creating PJMEDIA video format manager"));
	return status;
    }

    /* Create video codec manager singleton */
    status = pjmedia_vid_codec_mgr_create(pjsua_var.pool, &mgr);
    if (status != PJ_SUCCESS) {
	PJ_PERROR(1,(THIS_FILE, status,
		     "Error creating PJMEDIA video codec manager"));
	return status;
    }

    /* Register our codecs */
    alt_vid_codec_factory.base.op = &alt_vid_codec_factory_op;
    alt_vid_codec_factory.base.factory_data = NULL;

    status = pjmedia_vid_codec_mgr_register_factory(mgr, &alt_vid_codec_factory.base);
    if (status != PJ_SUCCESS)
	return status;

    /*
     * TODO: put your 3rd party library initialization routine here
     */

    return PJ_SUCCESS;
}

/* Start the video library */
pj_status_t pjsua_vid_subsys_start(void)
{
    /*
     * TODO: put your 3rd party library startup routine here
     */
    return PJ_SUCCESS;
}

/* Cleanup and deinitialize the video library */
pj_status_t pjsua_vid_subsys_destroy(void)
{
    /*
     * TODO: put your 3rd party library cleanup routine here
     */
    return PJ_SUCCESS;
}

/* Initialize video call media */
pj_status_t pjsua_vid_channel_init(pjsua_call_media *call_med)
{
    /*
     * TODO: put call media initialization
     */
    return PJ_SUCCESS;
}

/* Internal function to stop video stream */
void pjsua_vid_stop_stream(pjsua_call_media *call_med)
{
    PJ_LOG(4,(THIS_FILE, "Stopping video stream.."));

    if (call_med->tp) {
	pjmedia_transport_detach(call_med->tp, call_med);
    }

    /*
     * TODO:
     *   - stop your video stream here
     */

}

/* Our callback to receive incoming RTP packets */
static void vid_rtp_cb(void *user_data, void *pkt, pj_ssize_t size)
{
    pjsua_call_media *call_med = (pjsua_call_media*) user_data;

    /* TODO: Do something with the packet */
    PJ_LOG(4,(THIS_FILE, "RX %d bytes video RTP packet", (int)size));
}

/* Our callback to receive RTCP packets */
static void vid_rtcp_cb(void *user_data, void *pkt, pj_ssize_t size)
{
    pjsua_call_media *call_med = (pjsua_call_media*) user_data;

    /* TODO: Do something with the packet here */
    PJ_LOG(4,(THIS_FILE, "RX %d bytes video RTCP packet", (int)size));
}

/* A demo function to send dummy "RTP" packets periodically. You would not
 * need to have this function in the real app!
 */
static void timer_to_send_vid_rtp(void *user_data)
{
    pjsua_call_media *call_med = (pjsua_call_media*) user_data;
    const char *pkt = "Not RTP packet";

    if (call_med->call->inv == NULL) {
	/* Call has been disconnected. There is race condition here as
	 * this cb may be called sometime after call has been disconnected */
	return;
    }

    pjmedia_transport_send_rtp(call_med->tp, pkt, strlen(pkt));

    pjsua_schedule_timer2(&timer_to_send_vid_rtp, call_med, 2000);
}

static void timer_to_send_vid_rtcp(void *user_data)
{
    pjsua_call_media *call_med = (pjsua_call_media*) user_data;
    const char *pkt = "Not RTCP packet";

    if (call_med->call->inv == NULL) {
	/* Call has been disconnected. There is race condition here as
	 * this cb may be called sometime after call has been disconnected */
	return;
    }

    pjmedia_transport_send_rtcp(call_med->tp, pkt, strlen(pkt));

    pjsua_schedule_timer2(&timer_to_send_vid_rtcp, call_med, 5000);
}

/* update video channel after SDP negotiation */
pj_status_t pjsua_vid_channel_update(pjsua_call_media *call_med,
				     pj_pool_t *tmp_pool,
				     pjmedia_vid_stream_info *si,
				     const pjmedia_sdp_session *local_sdp,
				     const pjmedia_sdp_session *remote_sdp)
{
    pj_status_t status;
    
    PJ_LOG(4,(THIS_FILE, "Video channel update.."));
    pj_log_push_indent();

    /* Check if no media is active */
    if (si->dir != PJMEDIA_DIR_NONE) {
	/* Attach our RTP and RTCP callbacks to the media transport */
	status = pjmedia_transport_attach(call_med->tp, call_med,
	                                  &si->rem_addr, &si->rem_rtcp,
	                                  pj_sockaddr_get_len(&si->rem_addr),
	                                  &vid_rtp_cb, &vid_rtcp_cb);
	/*
	 * TODO:
	 *   - Create and start your video stream based on the parameters
	 *     in si
	 */

	/* For a demonstration, let's use a timer to send "RTP" packet
	 * periodically.
	 */
	pjsua_schedule_timer2(&timer_to_send_vid_rtp, call_med, 1000);
	pjsua_schedule_timer2(&timer_to_send_vid_rtcp, call_med, 3500);
    }

    pj_log_pop_indent();
    return PJ_SUCCESS;
}


/*****************************************************************************
 * Preview
 */

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
}

PJ_DEF(pjsua_vid_win_id) pjsua_vid_preview_get_win(pjmedia_vid_dev_index id)
{
    UNIMPLEMENTED(pjsua_vid_preview_get_win)
    return PJSUA_INVALID_ID;
}

/* Reset internal window structure. Not sure if this is needed?. */
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

/* Does it have built-in preview support. */
PJ_DEF(pj_bool_t) pjsua_vid_preview_has_native(pjmedia_vid_dev_index id)
{
    UNIMPLEMENTED(pjsua_vid_preview_has_native)
    return PJ_FALSE;
}

/* Start video preview window for the specified capture device. */
PJ_DEF(pj_status_t) pjsua_vid_preview_start(pjmedia_vid_dev_index id,
                                            const pjsua_vid_preview_param *prm)
{
    UNIMPLEMENTED(pjsua_vid_preview_start)
    return PJ_ENOTSUP;
}

/* Stop video preview. */
PJ_DEF(pj_status_t) pjsua_vid_preview_stop(pjmedia_vid_dev_index id)
{
    UNIMPLEMENTED(pjsua_vid_preview_stop)
    return PJ_ENOTSUP;
}


/*****************************************************************************
 * Devices.
 */

/* Get the number of video devices installed in the system. */
PJ_DEF(unsigned) pjsua_vid_dev_count(void)
{
    UNIMPLEMENTED(pjsua_vid_dev_count)
    return 0;
}

/* Retrieve the video device info for the specified device index. */
PJ_DEF(pj_status_t) pjsua_vid_dev_get_info(pjmedia_vid_dev_index id,
                                           pjmedia_vid_dev_info *vdi)
{
    UNIMPLEMENTED(pjsua_vid_dev_get_info)
    return PJ_ENOTSUP;
}

/* Enum all video devices installed in the system. */
PJ_DEF(pj_status_t) pjsua_vid_enum_devs(pjmedia_vid_dev_info info[],
					unsigned *count)
{
    UNIMPLEMENTED(pjsua_vid_enum_devs)
    return PJ_ENOTSUP;
}


/*****************************************************************************
 * Codecs.
 */

/* Enum all supported video codecs in the system. */
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

/* Change video codec priority. */
PJ_DEF(pj_status_t) pjsua_vid_codec_set_priority( const pj_str_t *codec_id,
						  pj_uint8_t priority )
{
    UNIMPLEMENTED(pjsua_vid_codec_set_priority)
    return PJ_ENOTSUP;
}

/* Get video codec parameters. */
PJ_DEF(pj_status_t) pjsua_vid_codec_get_param(
					const pj_str_t *codec_id,
					pjmedia_vid_codec_param *param)
{
    UNIMPLEMENTED(pjsua_vid_codec_get_param)
    return PJ_ENOTSUP;
}

/* Set video codec parameters. */
PJ_DEF(pj_status_t) pjsua_vid_codec_set_param(
					const pj_str_t *codec_id,
					const pjmedia_vid_codec_param *param)
{
    UNIMPLEMENTED(pjsua_vid_codec_set_param)
    return PJ_ENOTSUP;
}


/*****************************************************************************
 * Window
 */

/* Enumerates all video windows. */
PJ_DEF(pj_status_t) pjsua_vid_enum_wins( pjsua_vid_win_id wids[],
					 unsigned *count)
{
    UNIMPLEMENTED(pjsua_vid_enum_wins)
    return PJ_ENOTSUP;
}

/* Get window info. */
PJ_DEF(pj_status_t) pjsua_vid_win_get_info( pjsua_vid_win_id wid,
                                            pjsua_vid_win_info *wi)
{
    UNIMPLEMENTED(pjsua_vid_win_get_info)
    return PJ_ENOTSUP;
}

/* Show or hide window. */
PJ_DEF(pj_status_t) pjsua_vid_win_set_show( pjsua_vid_win_id wid,
                                            pj_bool_t show)
{
    UNIMPLEMENTED(pjsua_vid_win_set_show)
    return PJ_ENOTSUP;
}

/* Set video window position. */
PJ_DEF(pj_status_t) pjsua_vid_win_set_pos( pjsua_vid_win_id wid,
                                           const pjmedia_coord *pos)
{
    UNIMPLEMENTED(pjsua_vid_win_set_pos)
    return PJ_ENOTSUP;
}

/* Resize window. */
PJ_DEF(pj_status_t) pjsua_vid_win_set_size( pjsua_vid_win_id wid,
                                            const pjmedia_rect_size *size)
{
    UNIMPLEMENTED(pjsua_vid_win_set_size)
    return PJ_ENOTSUP;
}

/* Set video orientation. */
PJ_DEF(pj_status_t) pjsua_vid_win_rotate( pjsua_vid_win_id wid,
                                          int angle)
{
    UNIMPLEMENTED(pjsua_vid_win_rotate)
    return PJ_ENOTSUP;
}

/* Start, stop, and/or manipulate video transmission for the specified call. */
PJ_DEF(pj_status_t) pjsua_call_set_vid_strm (
				pjsua_call_id call_id,
				pjsua_call_vid_strm_op op,
				const pjsua_call_vid_strm_op_param *param)
{
    UNIMPLEMENTED(pjsua_call_set_vid_strm)
    return PJ_ENOTSUP;
}


/* Get the media stream index of the default video stream in the call. */
PJ_DEF(int) pjsua_call_get_vid_stream_idx(pjsua_call_id call_id)
{
    UNIMPLEMENTED(pjsua_call_get_vid_stream_idx)
    return -1;
}

/* Determine if video stream for the specified call is currently running
 * for the specified direction.
 */
PJ_DEF(pj_bool_t) pjsua_call_vid_stream_is_running( pjsua_call_id call_id,
                                                    int med_idx,
                                                    pjmedia_dir dir)
{
    UNIMPLEMENTED(pjsua_call_vid_stream_is_running)
    return PJ_FALSE;
}

#endif	/* PJSUA_HAS_VIDEO */

