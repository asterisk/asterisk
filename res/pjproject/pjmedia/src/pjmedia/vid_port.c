/* $Id$ */
/*
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
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
#include <pjmedia/vid_port.h>
#include <pjmedia/clock.h>
#include <pjmedia/converter.h>
#include <pjmedia/errno.h>
#include <pjmedia/event.h>
#include <pjmedia/vid_codec.h>
#include <pj/log.h>
#include <pj/pool.h>


#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)


#define SIGNATURE	PJMEDIA_SIG_VID_PORT
#define THIS_FILE	"vid_port.c"

typedef struct vid_pasv_port vid_pasv_port;

enum role
{
    ROLE_NONE,
    ROLE_ACTIVE,
    ROLE_PASSIVE
};

struct pjmedia_vid_port
{
    pj_pool_t               *pool;
    pj_str_t                 dev_name;
    pjmedia_dir              dir;
//    pjmedia_rect_size        cap_size;
    pjmedia_vid_dev_stream  *strm;
    pjmedia_vid_dev_cb       strm_cb;
    void                    *strm_cb_data;
    enum role                role,
                             stream_role;
    vid_pasv_port           *pasv_port;
    pjmedia_port            *client_port;
    pj_bool_t                destroy_client_port;

    struct {
        pjmedia_converter	*conv;
        void		        *conv_buf;
        pj_size_t		 conv_buf_size;
        pjmedia_conversion_param conv_param;
        unsigned                 usec_ctr;
        unsigned                 usec_src, usec_dst;
    } conv;

    pjmedia_clock           *clock;
    pjmedia_clock_src        clocksrc;

    struct sync_clock_src_t
    {
        pjmedia_clock_src   *sync_clocksrc;
        pj_int32_t           sync_delta;
        unsigned             max_sync_ticks;
        unsigned             nsync_frame;
        unsigned             nsync_progress;
    } sync_clocksrc;

    pjmedia_frame           *frm_buf;
    pj_size_t                frm_buf_size;
    pj_mutex_t              *frm_mutex;
};

struct vid_pasv_port
{
    pjmedia_port	 base;
    pjmedia_vid_port	*vp;
};

static pj_status_t vidstream_cap_cb(pjmedia_vid_dev_stream *stream,
				    void *user_data,
				    pjmedia_frame *frame);
static pj_status_t vidstream_render_cb(pjmedia_vid_dev_stream *stream,
				       void *user_data,
				       pjmedia_frame *frame);
static pj_status_t vidstream_event_cb(pjmedia_event *event,
                                      void *user_data);
static pj_status_t client_port_event_cb(pjmedia_event *event,
                                        void *user_data);

static void enc_clock_cb(const pj_timestamp *ts, void *user_data);
static void dec_clock_cb(const pj_timestamp *ts, void *user_data);

static pj_status_t vid_pasv_port_put_frame(struct pjmedia_port *this_port,
					   pjmedia_frame *frame);

static pj_status_t vid_pasv_port_get_frame(struct pjmedia_port *this_port,
					   pjmedia_frame *frame);


PJ_DEF(void) pjmedia_vid_port_param_default(pjmedia_vid_port_param *prm)
{
    pj_bzero(prm, sizeof(*prm));
    prm->active = PJ_TRUE;
}

static const char *vid_dir_name(pjmedia_dir dir)
{
    switch (dir) {
    case PJMEDIA_DIR_CAPTURE:
	return "capture";
    case PJMEDIA_DIR_RENDER:
	return "render";
    default:
	return "??";
    }
}

static pj_status_t create_converter(pjmedia_vid_port *vp)
{
    if (vp->conv.conv) {
        pjmedia_converter_destroy(vp->conv.conv);
	vp->conv.conv = NULL;
    }

    /* Instantiate converter if necessary */
    if (vp->conv.conv_param.src.id != vp->conv.conv_param.dst.id ||
	(vp->conv.conv_param.src.det.vid.size.w !=
         vp->conv.conv_param.dst.det.vid.size.w) ||
	(vp->conv.conv_param.src.det.vid.size.h !=
         vp->conv.conv_param.dst.det.vid.size.h))
    {
	pj_status_t status;

	/* Yes, we need converter */
	status = pjmedia_converter_create(NULL, vp->pool, &vp->conv.conv_param,
					  &vp->conv.conv);
	if (status != PJ_SUCCESS) {
	    PJ_PERROR(4,(THIS_FILE, status, "Error creating converter"));
	    return status;
	}
    }

    if (vp->conv.conv ||
        (vp->role==ROLE_ACTIVE && (vp->dir & PJMEDIA_DIR_ENCODING)))
    {
	pj_status_t status;
	const pjmedia_video_format_info *vfi;
	pjmedia_video_apply_fmt_param vafp;

	/* Allocate buffer for conversion */
	vfi = pjmedia_get_video_format_info(NULL, vp->conv.conv_param.dst.id);
	if (!vfi)
	    return PJMEDIA_EBADFMT;

	pj_bzero(&vafp, sizeof(vafp));
	vafp.size = vp->conv.conv_param.dst.det.vid.size;
	status = vfi->apply_fmt(vfi, &vafp);
	if (status != PJ_SUCCESS)
	    return PJMEDIA_EBADFMT;

	if (vafp.framebytes > vp->conv.conv_buf_size) {
	    vp->conv.conv_buf = pj_pool_alloc(vp->pool, vafp.framebytes);
	    vp->conv.conv_buf_size = vafp.framebytes;
	}
    }

    vp->conv.usec_ctr = 0;
    vp->conv.usec_src = PJMEDIA_PTIME(&vp->conv.conv_param.src.det.vid.fps);
    vp->conv.usec_dst = PJMEDIA_PTIME(&vp->conv.conv_param.dst.det.vid.fps);

    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t) pjmedia_vid_port_create( pj_pool_t *pool,
					     const pjmedia_vid_port_param *prm,
					     pjmedia_vid_port **p_vid_port)
{
    pjmedia_vid_port *vp;
    const pjmedia_video_format_detail *vfd;
    char dev_name[64];
    char fmt_name[5];
    pjmedia_vid_dev_cb vid_cb;
    pj_bool_t need_frame_buf = PJ_FALSE;
    pj_status_t status;
    unsigned ptime_usec;
    pjmedia_vid_dev_param vparam;
    pjmedia_vid_dev_info di;
    unsigned i;

    PJ_ASSERT_RETURN(pool && prm && p_vid_port, PJ_EINVAL);
    PJ_ASSERT_RETURN(prm->vidparam.fmt.type == PJMEDIA_TYPE_VIDEO &&
                     prm->vidparam.dir != PJMEDIA_DIR_NONE &&
                     prm->vidparam.dir != PJMEDIA_DIR_CAPTURE_RENDER,
		     PJ_EINVAL);

    /* Retrieve the video format detail */
    vfd = pjmedia_format_get_video_format_detail(&prm->vidparam.fmt, PJ_TRUE);
    if (!vfd)
	return PJ_EINVAL;

    PJ_ASSERT_RETURN(vfd->fps.num, PJ_EINVAL);

    /* Allocate videoport */
    vp = PJ_POOL_ZALLOC_T(pool, pjmedia_vid_port);
    vp->pool = pj_pool_create(pool->factory, "video port", 500, 500, NULL);
    vp->role = prm->active ? ROLE_ACTIVE : ROLE_PASSIVE;
    vp->dir = prm->vidparam.dir;
//    vp->cap_size = vfd->size;

    vparam = prm->vidparam;
    dev_name[0] = '\0';

    /* Get device info */
    if (vp->dir & PJMEDIA_DIR_CAPTURE)
        status = pjmedia_vid_dev_get_info(prm->vidparam.cap_id, &di);
    else
        status = pjmedia_vid_dev_get_info(prm->vidparam.rend_id, &di);
    if (status != PJ_SUCCESS)
        return status;

    pj_ansi_snprintf(dev_name, sizeof(dev_name), "%s [%s]",
                     di.name, di.driver);

    for (i = 0; i < di.fmt_cnt; ++i) {
        if (prm->vidparam.fmt.id == di.fmt[i].id)
            break;
    }

    if (i == di.fmt_cnt) {
        /* The device has no no matching format. Pick one from
         * the supported formats, and later use converter to
         * convert it to the required format.
         */
        pj_assert(di.fmt_cnt != 0);
        vparam.fmt.id = di.fmt[0].id;
    }

    pj_strdup2_with_null(pool, &vp->dev_name, di.name);
    vp->stream_role = di.has_callback ? ROLE_ACTIVE : ROLE_PASSIVE;

    pjmedia_fourcc_name(vparam.fmt.id, fmt_name);

    PJ_LOG(4,(THIS_FILE,
	      "Opening device %s for %s: format=%s, size=%dx%d @%d:%d fps",
	      dev_name,
	      vid_dir_name(prm->vidparam.dir), fmt_name,
	      vfd->size.w, vfd->size.h,
	      vfd->fps.num, vfd->fps.denum));

    ptime_usec = PJMEDIA_PTIME(&vfd->fps);
    pjmedia_clock_src_init(&vp->clocksrc, PJMEDIA_TYPE_VIDEO,
                           prm->vidparam.clock_rate, ptime_usec);
    vp->sync_clocksrc.max_sync_ticks = 
        PJMEDIA_CLOCK_SYNC_MAX_RESYNC_DURATION *
        1000 / vp->clocksrc.ptime_usec;

    /* Create the video stream */
    pj_bzero(&vid_cb, sizeof(vid_cb));
    vid_cb.capture_cb = &vidstream_cap_cb;
    vid_cb.render_cb = &vidstream_render_cb;

    status = pjmedia_vid_dev_stream_create(&vparam, &vid_cb, vp,
				           &vp->strm);
    if (status != PJ_SUCCESS)
	goto on_error;

    PJ_LOG(4,(THIS_FILE,
	      "Device %s opened: format=%s, size=%dx%d @%d:%d fps",
	      dev_name, fmt_name,
	      vparam.fmt.det.vid.size.w, vparam.fmt.det.vid.size.h,
	      vparam.fmt.det.vid.fps.num, vparam.fmt.det.vid.fps.denum));

    /* Subscribe to device's events */
    pjmedia_event_subscribe(NULL, &vidstream_event_cb,
                            vp, vp->strm);

    if (vp->dir & PJMEDIA_DIR_CAPTURE) {
	pjmedia_format_copy(&vp->conv.conv_param.src, &vparam.fmt);
	pjmedia_format_copy(&vp->conv.conv_param.dst, &prm->vidparam.fmt);
    } else {
	pjmedia_format_copy(&vp->conv.conv_param.src, &prm->vidparam.fmt);
	pjmedia_format_copy(&vp->conv.conv_param.dst, &vparam.fmt);
    }

    status = create_converter(vp);
    if (status != PJ_SUCCESS)
	goto on_error;

    if (vp->role==ROLE_ACTIVE &&
        ((vp->dir & PJMEDIA_DIR_ENCODING) || vp->stream_role==ROLE_PASSIVE))
    {
        pjmedia_clock_param param;

	/* Active role is wanted, but our device is passive, so create
	 * master clocks to run the media flow. For encoding direction,
         * we also want to create our own clock since the device's clock
         * may run at a different rate.
	 */
	need_frame_buf = PJ_TRUE;
            
        param.usec_interval = PJMEDIA_PTIME(&vfd->fps);
        param.clock_rate = prm->vidparam.clock_rate;
        status = pjmedia_clock_create2(pool, &param,
                                       PJMEDIA_CLOCK_NO_HIGHEST_PRIO,
                                       (vp->dir & PJMEDIA_DIR_ENCODING) ?
                                       &enc_clock_cb: &dec_clock_cb,
                                       vp, &vp->clock);
        if (status != PJ_SUCCESS)
            goto on_error;

    } else if (vp->role==ROLE_PASSIVE) {
	vid_pasv_port *pp;

	/* Always need to create media port for passive role */
	vp->pasv_port = pp = PJ_POOL_ZALLOC_T(pool, vid_pasv_port);
	pp->vp = vp;
	pp->base.get_frame = &vid_pasv_port_get_frame;
	pp->base.put_frame = &vid_pasv_port_put_frame;
	pjmedia_port_info_init2(&pp->base.info, &vp->dev_name,
	                        PJMEDIA_SIG_VID_PORT,
			        prm->vidparam.dir, &prm->vidparam.fmt);

	if (vp->stream_role == ROLE_ACTIVE) {
	    need_frame_buf = PJ_TRUE;
	}
    }

    if (need_frame_buf) {
	const pjmedia_video_format_info *vfi;
	pjmedia_video_apply_fmt_param vafp;

	vfi = pjmedia_get_video_format_info(NULL, vparam.fmt.id);
	if (!vfi) {
	    status = PJ_ENOTFOUND;
	    goto on_error;
	}

	pj_bzero(&vafp, sizeof(vafp));
	vafp.size = vparam.fmt.det.vid.size;
	status = vfi->apply_fmt(vfi, &vafp);
	if (status != PJ_SUCCESS)
	    goto on_error;

        vp->frm_buf = PJ_POOL_ZALLOC_T(pool, pjmedia_frame);
        vp->frm_buf_size = vafp.framebytes;
        vp->frm_buf->buf = pj_pool_alloc(pool, vafp.framebytes);
        vp->frm_buf->size = vp->frm_buf_size;
        vp->frm_buf->type = PJMEDIA_FRAME_TYPE_NONE;

        status = pj_mutex_create_simple(pool, vp->dev_name.ptr,
                                        &vp->frm_mutex);
        if (status != PJ_SUCCESS)
            goto on_error;
    }

    *p_vid_port = vp;

    return PJ_SUCCESS;

on_error:
    pjmedia_vid_port_destroy(vp);
    return status;
}

PJ_DEF(void) pjmedia_vid_port_set_cb(pjmedia_vid_port *vid_port,
				     const pjmedia_vid_dev_cb *cb,
                                     void *user_data)
{
    pj_assert(vid_port && cb);
    pj_memcpy(&vid_port->strm_cb, cb, sizeof(*cb));
    vid_port->strm_cb_data = user_data;
}

PJ_DEF(pjmedia_vid_dev_stream*)
pjmedia_vid_port_get_stream(pjmedia_vid_port *vp)
{
    PJ_ASSERT_RETURN(vp, NULL);
    return vp->strm;
}


PJ_DEF(pjmedia_port*)
pjmedia_vid_port_get_passive_port(pjmedia_vid_port *vp)
{
    PJ_ASSERT_RETURN(vp && vp->role==ROLE_PASSIVE, NULL);
    return &vp->pasv_port->base;
}


PJ_DEF(pjmedia_clock_src *)
pjmedia_vid_port_get_clock_src( pjmedia_vid_port *vid_port )
{
    PJ_ASSERT_RETURN(vid_port, NULL);
    return &vid_port->clocksrc;
}

PJ_DECL(pj_status_t)
pjmedia_vid_port_set_clock_src( pjmedia_vid_port *vid_port,
                                pjmedia_clock_src *clocksrc)
{
    PJ_ASSERT_RETURN(vid_port && clocksrc, PJ_EINVAL);

    vid_port->sync_clocksrc.sync_clocksrc = clocksrc;
    vid_port->sync_clocksrc.sync_delta =
        pjmedia_clock_src_get_time_msec(&vid_port->clocksrc) -
        pjmedia_clock_src_get_time_msec(clocksrc);

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_vid_port_connect(pjmedia_vid_port *vp,
					      pjmedia_port *port,
					      pj_bool_t destroy)
{
    PJ_ASSERT_RETURN(vp && vp->role==ROLE_ACTIVE, PJ_EINVAL);
    vp->destroy_client_port = destroy;
    vp->client_port = port;

    /* Subscribe to client port's events */
    pjmedia_event_subscribe(NULL, &client_port_event_cb, vp,
                            vp->client_port);

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_vid_port_disconnect(pjmedia_vid_port *vp)
{
    PJ_ASSERT_RETURN(vp && vp->role==ROLE_ACTIVE, PJ_EINVAL);

    pjmedia_event_unsubscribe(NULL, &client_port_event_cb, vp,
                              vp->client_port);
    vp->client_port = NULL;

    return PJ_SUCCESS;
}


PJ_DEF(pjmedia_port*)
pjmedia_vid_port_get_connected_port(pjmedia_vid_port *vp)
{
    PJ_ASSERT_RETURN(vp && vp->role==ROLE_ACTIVE, NULL);
    return vp->client_port;
}

PJ_DEF(pj_status_t) pjmedia_vid_port_start(pjmedia_vid_port *vp)
{
    pj_status_t status;

    PJ_ASSERT_RETURN(vp, PJ_EINVAL);

    status = pjmedia_vid_dev_stream_start(vp->strm);
    if (status != PJ_SUCCESS)
	goto on_error;

    if (vp->clock) {
	status = pjmedia_clock_start(vp->clock);
	if (status != PJ_SUCCESS)
	    goto on_error;
    }

    return PJ_SUCCESS;

on_error:
    pjmedia_vid_port_stop(vp);
    return status;
}

PJ_DEF(pj_bool_t) pjmedia_vid_port_is_running(pjmedia_vid_port *vp)
{
    return pjmedia_vid_dev_stream_is_running(vp->strm);
}

PJ_DEF(pj_status_t) pjmedia_vid_port_stop(pjmedia_vid_port *vp)
{
    pj_status_t status;

    PJ_ASSERT_RETURN(vp, PJ_EINVAL);

    if (vp->clock) {
	status = pjmedia_clock_stop(vp->clock);
    }

    status = pjmedia_vid_dev_stream_stop(vp->strm);

    return status;
}

PJ_DEF(void) pjmedia_vid_port_destroy(pjmedia_vid_port *vp)
{
    PJ_ASSERT_ON_FAIL(vp, return);

    PJ_LOG(4,(THIS_FILE, "Closing %s..", vp->dev_name.ptr));

    if (vp->clock) {
	pjmedia_clock_destroy(vp->clock);
	vp->clock = NULL;
    }
    if (vp->strm) {
        pjmedia_event_unsubscribe(NULL, &vidstream_event_cb, vp, vp->strm);
	pjmedia_vid_dev_stream_destroy(vp->strm);
	vp->strm = NULL;
    }
    if (vp->client_port) {
        pjmedia_event_unsubscribe(NULL, &client_port_event_cb, vp,
                                  vp->client_port);
	if (vp->destroy_client_port)
	    pjmedia_port_destroy(vp->client_port);
	vp->client_port = NULL;
    }
    if (vp->frm_mutex) {
	pj_mutex_destroy(vp->frm_mutex);
	vp->frm_mutex = NULL;
    }
    if (vp->conv.conv) {
        pjmedia_converter_destroy(vp->conv.conv);
        vp->conv.conv = NULL;
    }
    pj_pool_release(vp->pool);
}

/*
static void save_rgb_frame(int width, int height, const pjmedia_frame *frm)
{
    static int counter;
    FILE *pFile;
    char szFilename[32];
    const pj_uint8_t *pFrame = (const pj_uint8_t*)frm->buf;
    int  y;

    if (counter > 10)
	return;

    // Open file
    sprintf(szFilename, "frame%02d.ppm", counter++);
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL)
      return;

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // Write pixel data
    for(y=0; y<height; y++)
      fwrite(pFrame+y*width*3, 1, width*3, pFile);

    // Close file
    fclose(pFile);
}
*/

/* Handle event from vidstream */
static pj_status_t vidstream_event_cb(pjmedia_event *event,
                                      void *user_data)
{
    pjmedia_vid_port *vp = (pjmedia_vid_port*)user_data;
    
    /* Just republish the event to our client */
    return pjmedia_event_publish(NULL, vp, event, 0);
}

static pj_status_t client_port_event_cb(pjmedia_event *event,
                                        void *user_data)
{
    pjmedia_vid_port *vp = (pjmedia_vid_port*)user_data;

    if (event->type == PJMEDIA_EVENT_FMT_CHANGED) {
        const pjmedia_video_format_detail *vfd;
        pjmedia_vid_dev_param vid_param;
        pj_status_t status;
        
	pjmedia_vid_port_stop(vp);
        
        /* Retrieve the video format detail */
        vfd = pjmedia_format_get_video_format_detail(
                  &event->data.fmt_changed.new_fmt, PJ_TRUE);
        if (!vfd || !vfd->fps.num || !vfd->fps.denum)
            return PJMEDIA_EVID_BADFORMAT;
        
	/* Change the destination format to the new format */
	pjmedia_format_copy(&vp->conv.conv_param.src,
			    &event->data.fmt_changed.new_fmt);
	/* Only copy the size here */
	vp->conv.conv_param.dst.det.vid.size =
	    event->data.fmt_changed.new_fmt.det.vid.size;

	status = create_converter(vp);
	if (status != PJ_SUCCESS) {
	    PJ_PERROR(4,(THIS_FILE, status, "Error recreating converter"));
	    return status;
	}

        pjmedia_vid_dev_stream_get_param(vp->strm, &vid_param);
        if (vid_param.fmt.id != vp->conv.conv_param.dst.id ||
            (vid_param.fmt.det.vid.size.h !=
             vp->conv.conv_param.dst.det.vid.size.h) ||
            (vid_param.fmt.det.vid.size.w !=
             vp->conv.conv_param.dst.det.vid.size.w))
        {
            status = pjmedia_vid_dev_stream_set_cap(vp->strm,
                                                    PJMEDIA_VID_DEV_CAP_FORMAT,
                                                    &vp->conv.conv_param.dst);
            if (status != PJ_SUCCESS) {
                PJ_LOG(3, (THIS_FILE, "failure in changing the format of the "
                                      "video device"));
                PJ_LOG(3, (THIS_FILE, "reverting to its original format: %s",
                                      status != PJMEDIA_EVID_ERR ? "success" :
                                      "failure"));
                return status;
            }
        }
        
        if (vp->stream_role == ROLE_PASSIVE) {
            pjmedia_clock_param clock_param;
            
            /**
             * Initially, frm_buf was allocated the biggest
             * supported size, so we do not need to re-allocate
             * the buffer here.
             */
            /* Adjust the clock */
            clock_param.usec_interval = PJMEDIA_PTIME(&vfd->fps);
            clock_param.clock_rate = vid_param.clock_rate;
            pjmedia_clock_modify(vp->clock, &clock_param);
        }
        
	pjmedia_vid_port_start(vp);
    }
    
    /* Republish the event, post the event to the event manager
     * to avoid deadlock if vidport is trying to stop the clock.
     */
    return pjmedia_event_publish(NULL, vp, event,
                                 PJMEDIA_EVENT_PUBLISH_POST_EVENT);
}

static pj_status_t convert_frame(pjmedia_vid_port *vp,
                                 pjmedia_frame *src_frame,
                                 pjmedia_frame *dst_frame)
{
    pj_status_t status = PJ_SUCCESS;

    if (vp->conv.conv) {
	dst_frame->buf  = vp->conv.conv_buf;
	dst_frame->size = vp->conv.conv_buf_size;
	status = pjmedia_converter_convert(vp->conv.conv,
					   src_frame, dst_frame);
    }
    
    return status;
}

/* Copy frame to buffer. */
static void copy_frame_to_buffer(pjmedia_vid_port *vp,
                                 pjmedia_frame *frame)
{
    pj_mutex_lock(vp->frm_mutex);
    pjmedia_frame_copy(vp->frm_buf, frame);
    pj_mutex_unlock(vp->frm_mutex);
}

/* Get frame from buffer and convert it if necessary. */
static pj_status_t get_frame_from_buffer(pjmedia_vid_port *vp,
                                         pjmedia_frame *frame)
{
    pj_status_t status = PJ_SUCCESS;

    pj_mutex_lock(vp->frm_mutex);
    if (vp->conv.conv)
        status = convert_frame(vp, vp->frm_buf, frame);
    else
        pjmedia_frame_copy(frame, vp->frm_buf);
    pj_mutex_unlock(vp->frm_mutex);
    
    return status;
}

static void enc_clock_cb(const pj_timestamp *ts, void *user_data)
{
    /* We are here because user wants us to be active but the stream is
     * passive. So get a frame from the stream and push it to user.
     */
    pjmedia_vid_port *vp = (pjmedia_vid_port*)user_data;
    pjmedia_frame frame_;
    pj_status_t status = PJ_SUCCESS;

    pj_assert(vp->role==ROLE_ACTIVE);

    PJ_UNUSED_ARG(ts);

    if (!vp->client_port)
	return;

    if (vp->stream_role == ROLE_PASSIVE) {
        while (vp->conv.usec_ctr < vp->conv.usec_dst) {
            vp->frm_buf->size = vp->frm_buf_size;
            status = pjmedia_vid_dev_stream_get_frame(vp->strm, vp->frm_buf);
            vp->conv.usec_ctr += vp->conv.usec_src;
        }
        vp->conv.usec_ctr -= vp->conv.usec_dst;
        if (status != PJ_SUCCESS)
	    return;
    }

    //save_rgb_frame(vp->cap_size.w, vp->cap_size.h, vp->frm_buf);

    frame_.buf = vp->conv.conv_buf;
    frame_.size = vp->conv.conv_buf_size;
    status = get_frame_from_buffer(vp, &frame_);
    if (status != PJ_SUCCESS)
        return;

    status = pjmedia_port_put_frame(vp->client_port, &frame_);
    if (status != PJ_SUCCESS)
        return;
}

static void dec_clock_cb(const pj_timestamp *ts, void *user_data)
{
    /* We are here because user wants us to be active but the stream is
     * passive. So get a frame from the stream and push it to user.
     */
    pjmedia_vid_port *vp = (pjmedia_vid_port*)user_data;
    pj_status_t status;
    pjmedia_frame frame;

    pj_assert(vp->role==ROLE_ACTIVE && vp->stream_role==ROLE_PASSIVE);

    PJ_UNUSED_ARG(ts);

    if (!vp->client_port)
	return;

    status = vidstream_render_cb(vp->strm, vp, &frame);
    if (status != PJ_SUCCESS)
        return;
    
    if (frame.size > 0)
	status = pjmedia_vid_dev_stream_put_frame(vp->strm, &frame);
}

static pj_status_t vidstream_cap_cb(pjmedia_vid_dev_stream *stream,
				    void *user_data,
				    pjmedia_frame *frame)
{
    pjmedia_vid_port *vp = (pjmedia_vid_port*)user_data;

    /* We just store the frame in the buffer. For active role, we let
     * video port's clock to push the frame buffer to the user.
     * The decoding counterpart for passive role and active stream is
     * located in vid_pasv_port_put_frame()
     */
    copy_frame_to_buffer(vp, frame);

    /* This is tricky since the frame is still in its original unconverted
     * format, which may not be what the application expects.
     */
    if (vp->strm_cb.capture_cb)
        return (*vp->strm_cb.capture_cb)(stream, vp->strm_cb_data, frame);
    return PJ_SUCCESS;
}

static pj_status_t vidstream_render_cb(pjmedia_vid_dev_stream *stream,
				       void *user_data,
				       pjmedia_frame *frame)
{
    pjmedia_vid_port *vp = (pjmedia_vid_port*)user_data;
    pj_status_t status = PJ_SUCCESS;
    
    pj_bzero(frame, sizeof(pjmedia_frame));
    if (vp->role==ROLE_ACTIVE) {
        unsigned frame_ts = vp->clocksrc.clock_rate / 1000 *
                            vp->clocksrc.ptime_usec / 1000;

        if (!vp->client_port)
            return status;
        
        if (vp->sync_clocksrc.sync_clocksrc) {
            pjmedia_clock_src *src = vp->sync_clocksrc.sync_clocksrc;
            pj_int32_t diff;
            unsigned nsync_frame;
            
            /* Synchronization */
            /* Calculate the time difference (in ms) with the sync source */
            diff = pjmedia_clock_src_get_time_msec(&vp->clocksrc) -
                   pjmedia_clock_src_get_time_msec(src) -
                   vp->sync_clocksrc.sync_delta;
            
            /* Check whether sync source made a large jump */
            if (diff < 0 && -diff > PJMEDIA_CLOCK_SYNC_MAX_SYNC_MSEC) {
                pjmedia_clock_src_update(&vp->clocksrc, NULL);
                vp->sync_clocksrc.sync_delta = 
                    pjmedia_clock_src_get_time_msec(src) -
                    pjmedia_clock_src_get_time_msec(&vp->clocksrc);
                vp->sync_clocksrc.nsync_frame = 0;
                return status;
            }
            
            /* Calculate the difference (in frames) with the sync source */
            nsync_frame = abs(diff) * 1000 / vp->clocksrc.ptime_usec;
            if (nsync_frame == 0) {
                /* Nothing to sync */
                vp->sync_clocksrc.nsync_frame = 0;
            } else {
                pj_int32_t init_sync_frame = nsync_frame;
                
                /* Check whether it's a new sync or whether we need to reset
                 * the sync
                 */
                if (vp->sync_clocksrc.nsync_frame == 0 ||
                    (vp->sync_clocksrc.nsync_frame > 0 &&
                     nsync_frame > vp->sync_clocksrc.nsync_frame))
                {
                    vp->sync_clocksrc.nsync_frame = nsync_frame;
                    vp->sync_clocksrc.nsync_progress = 0;
                } else {
                    init_sync_frame = vp->sync_clocksrc.nsync_frame;
                }
                
                if (diff >= 0) {
                    unsigned skip_mod;
                    
                    /* We are too fast */
                    if (vp->sync_clocksrc.max_sync_ticks > 0) {
                        skip_mod = init_sync_frame / 
                        vp->sync_clocksrc.max_sync_ticks + 2;
                    } else
                        skip_mod = init_sync_frame + 2;
                    
                    PJ_LOG(5, (THIS_FILE, "synchronization: early by %d ms",
                               diff));
                    /* We'll play a frame every skip_mod-th tick instead of
                     * a complete pause
                     */
                    if (++vp->sync_clocksrc.nsync_progress % skip_mod > 0) {
                        pjmedia_clock_src_update(&vp->clocksrc, NULL);
                        return status;
                    }
                } else {
                    unsigned i, ndrop = init_sync_frame;
                    
                    /* We are too late, drop the frame */
                    if (vp->sync_clocksrc.max_sync_ticks > 0) {
                        ndrop /= vp->sync_clocksrc.max_sync_ticks;
                        ndrop++;
                    }
                    PJ_LOG(5, (THIS_FILE, "synchronization: late, "
                               "dropping %d frame(s)", ndrop));
                    
                    if (ndrop >= nsync_frame) {
                        vp->sync_clocksrc.nsync_frame = 0;
                        ndrop = nsync_frame;
                    } else
                        vp->sync_clocksrc.nsync_progress += ndrop;
                    
                    for (i = 0; i < ndrop; i++) {
                        vp->frm_buf->size = vp->frm_buf_size;
                        status = pjmedia_port_get_frame(vp->client_port,
                                                        vp->frm_buf);
                        if (status != PJ_SUCCESS) {
                            pjmedia_clock_src_update(&vp->clocksrc, NULL);
                            return status;
                        }
                        
                        pj_add_timestamp32(&vp->clocksrc.timestamp,
                                           frame_ts);
                    }
                }
            }
        }
        
        vp->frm_buf->size = vp->frm_buf_size;
        status = pjmedia_port_get_frame(vp->client_port, vp->frm_buf);
        if (status != PJ_SUCCESS) {
            pjmedia_clock_src_update(&vp->clocksrc, NULL);
            return status;
        }
        pj_add_timestamp32(&vp->clocksrc.timestamp, frame_ts);
        pjmedia_clock_src_update(&vp->clocksrc, NULL);

        status = convert_frame(vp, vp->frm_buf, frame);
	if (status != PJ_SUCCESS)
            return status;

	if (!vp->conv.conv)
	    pj_memcpy(frame, vp->frm_buf, sizeof(*frame));
    } else {
        /* The stream is active while we are passive so we need to get the
         * frame from the buffer.
         * The encoding counterpart is located in vid_pasv_port_get_frame()
         */
        get_frame_from_buffer(vp, frame);
    }
    if (vp->strm_cb.render_cb)
        return (*vp->strm_cb.render_cb)(stream, vp->strm_cb_data, frame);
    return PJ_SUCCESS;
}

static pj_status_t vid_pasv_port_put_frame(struct pjmedia_port *this_port,
					   pjmedia_frame *frame)
{
    struct vid_pasv_port *vpp = (struct vid_pasv_port*)this_port;
    pjmedia_vid_port *vp = vpp->vp;

    if (vp->stream_role==ROLE_PASSIVE) {
        /* We are passive and the stream is passive.
         * The encoding counterpart is in vid_pasv_port_get_frame().
         */
        pj_status_t status;
        pjmedia_frame frame_;
        
        status = convert_frame(vp, frame, &frame_);
        if (status != PJ_SUCCESS)
            return status;

	return pjmedia_vid_dev_stream_put_frame(vp->strm, (vp->conv.conv?
                                                           &frame_: frame));
    } else {
        /* We are passive while the stream is active so we just store the
         * frame in the buffer.
         * The encoding counterpart is located in vidstream_cap_cb()
         */
        copy_frame_to_buffer(vp, frame);
    }

    return PJ_SUCCESS;
}

static pj_status_t vid_pasv_port_get_frame(struct pjmedia_port *this_port,
					   pjmedia_frame *frame)
{
    struct vid_pasv_port *vpp = (struct vid_pasv_port*)this_port;
    pjmedia_vid_port *vp = vpp->vp;
    pj_status_t status = PJ_SUCCESS;

    if (vp->stream_role==ROLE_PASSIVE) {
        /* We are passive and the stream is passive.
         * The decoding counterpart is in vid_pasv_port_put_frame().
         */
	status = pjmedia_vid_dev_stream_get_frame(vp->strm, (vp->conv.conv?
                                                  vp->frm_buf: frame));
	if (status != PJ_SUCCESS)
	    return status;

        status = convert_frame(vp, vp->frm_buf, frame);
    } else {
        /* The stream is active while we are passive so we need to get the
         * frame from the buffer.
         * The decoding counterpart is located in vidstream_rend_cb()
         */
        get_frame_from_buffer(vp, frame);
    }

    return status;
}


#endif /* PJMEDIA_HAS_VIDEO */
