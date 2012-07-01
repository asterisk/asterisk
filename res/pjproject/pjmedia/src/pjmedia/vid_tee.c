/* $Id$ */
/* 
 * Copyright (C) 2011 Teluu Inc. (http://www.teluu.com)
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
#include <pjmedia/vid_tee.h>
#include <pjmedia/converter.h>
#include <pjmedia/errno.h>
#include <pj/array.h>
#include <pj/log.h>
#include <pj/pool.h>


#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)


#define TEE_PORT_NAME	"vid_tee"
#define TEE_PORT_SIGN	PJMEDIA_SIG_PORT_VID_TEE

#define THIS_FILE	"vid_tee.c"

typedef struct vid_tee_dst_port
{
    pjmedia_port	*dst;
    unsigned		 option;
} vid_tee_dst_port;


typedef struct vid_tee_port
{
    pjmedia_port	 base;
    pj_pool_t           *pool;
    pj_pool_factory     *pf;
    pj_pool_t           *buf_pool;
    void		*buf[2];
    unsigned             buf_cnt;
    pj_size_t		 buf_size;
    unsigned		 dst_port_maxcnt;
    unsigned		 dst_port_cnt;
    vid_tee_dst_port	*dst_ports;
    pj_uint8_t		*put_frm_flag;
    
    struct vid_tee_conv_t {
        pjmedia_converter   *conv;
        pj_size_t            conv_buf_size;        
    } *tee_conv;
} vid_tee_port;


static pj_status_t tee_put_frame(pjmedia_port *port, pjmedia_frame *frame);
static pj_status_t tee_get_frame(pjmedia_port *port, pjmedia_frame *frame);
static pj_status_t tee_destroy(pjmedia_port *port);

/*
 * Create a video tee port with the specified source media port.
 */
PJ_DEF(pj_status_t) pjmedia_vid_tee_create( pj_pool_t *pool,
					    const pjmedia_format *fmt,
					    unsigned max_dst_cnt,
					    pjmedia_port **p_vid_tee)
{
    vid_tee_port *tee;
    pj_str_t name_st;
    const pjmedia_video_format_info *vfi;
    pjmedia_video_apply_fmt_param vafp;
    pj_status_t status;

    PJ_ASSERT_RETURN(pool && fmt && p_vid_tee, PJ_EINVAL);
    PJ_ASSERT_RETURN(fmt->type == PJMEDIA_TYPE_VIDEO, PJ_EINVAL);

    /* Allocate video tee structure */
    tee = PJ_POOL_ZALLOC_T(pool, vid_tee_port);
    tee->pf = pool->factory;
    tee->pool = pj_pool_create(tee->pf, "video tee", 500, 500, NULL);

    /* Initialize video tee structure */
    tee->dst_port_maxcnt = max_dst_cnt;
    tee->dst_ports = (vid_tee_dst_port*)
                     pj_pool_calloc(pool, max_dst_cnt,
                                    sizeof(vid_tee_dst_port));
    tee->tee_conv = (struct vid_tee_conv_t *)
                    pj_pool_calloc(pool, max_dst_cnt,
                                   sizeof(struct vid_tee_conv_t));
    tee->put_frm_flag = (pj_uint8_t*)
			pj_pool_calloc(pool, max_dst_cnt,
				       sizeof(tee->put_frm_flag[0]));

    /* Initialize video tee buffer, its size is one frame */
    vfi = pjmedia_get_video_format_info(NULL, fmt->id);
    if (vfi == NULL)
	return PJMEDIA_EBADFMT;

    pj_bzero(&vafp, sizeof(vafp));
    vafp.size = fmt->det.vid.size;
    status = vfi->apply_fmt(vfi, &vafp);
    if (status != PJ_SUCCESS)
	return status;

    tee->buf_size = vafp.framebytes;

    /* Initialize video tee port */
    status = pjmedia_port_info_init2(&tee->base.info,
				     pj_strset2(&name_st, (char*)TEE_PORT_NAME),
				     TEE_PORT_SIGN,
				     PJMEDIA_DIR_ENCODING,
				     fmt);
    if (status != PJ_SUCCESS)
	return status;

    tee->base.get_frame = &tee_get_frame;
    tee->base.put_frame = &tee_put_frame;
    tee->base.on_destroy = &tee_destroy;

    /* Done */
    *p_vid_tee = &tee->base;

    return PJ_SUCCESS;
}

static void realloc_buf(vid_tee_port *vid_tee,
                        unsigned buf_cnt, pj_size_t buf_size)
{
    unsigned i;
    
    if (buf_cnt > vid_tee->buf_cnt)
        vid_tee->buf_cnt = buf_cnt;
    
    if (buf_size > vid_tee->buf_size) {
        /* We need a larger buffer here. */
        vid_tee->buf_size = buf_size;
        if (vid_tee->buf_pool) {
            pj_pool_release(vid_tee->buf_pool);
            vid_tee->buf_pool = NULL;
        }
        vid_tee->buf[0] = vid_tee->buf[1] = NULL;
    }
    
    if (!vid_tee->buf_pool) {
        vid_tee->buf_pool = pj_pool_create(vid_tee->pf, "video tee buffer",
                                           1000, 1000, NULL);
    }
 
    for (i = 0; i < vid_tee->buf_cnt; i++) {
        if (!vid_tee->buf[i])
            vid_tee->buf[i] = pj_pool_alloc(vid_tee->buf_pool,
                                            vid_tee->buf_size);
    }
}

/*
 * Add a destination media port to the video tee.
 */
PJ_DEF(pj_status_t) pjmedia_vid_tee_add_dst_port(pjmedia_port *vid_tee,
						 unsigned option,
						 pjmedia_port *port)
{
    vid_tee_port *tee = (vid_tee_port*)vid_tee;
    pjmedia_video_format_detail *vfd;

    PJ_ASSERT_RETURN(vid_tee && vid_tee->info.signature==TEE_PORT_SIGN,
		     PJ_EINVAL);

    if (tee->dst_port_cnt >= tee->dst_port_maxcnt)
	return PJ_ETOOMANY;
    
    if (vid_tee->info.fmt.id != port->info.fmt.id)
	return PJMEDIA_EBADFMT;

    vfd = pjmedia_format_get_video_format_detail(&port->info.fmt, PJ_TRUE);
    if (vfd->size.w != vid_tee->info.fmt.det.vid.size.w ||
	vfd->size.h != vid_tee->info.fmt.det.vid.size.h)
    {
        return PJMEDIA_EBADFMT;
    }
    
    realloc_buf(tee, (option & PJMEDIA_VID_TEE_DST_DO_IN_PLACE_PROC)?
                1: 0, tee->buf_size);

    pj_bzero(&tee->tee_conv[tee->dst_port_cnt], sizeof(tee->tee_conv[0]));
    tee->dst_ports[tee->dst_port_cnt].dst = port;
    tee->dst_ports[tee->dst_port_cnt].option = option;
    ++tee->dst_port_cnt;

    return PJ_SUCCESS;
}


/*
 * Add a destination media port to the video tee. Create a converter if
 * necessary.
 */
PJ_DEF(pj_status_t) pjmedia_vid_tee_add_dst_port2(pjmedia_port *vid_tee,
						  unsigned option,
						  pjmedia_port *port)
{
    vid_tee_port *tee = (vid_tee_port*)vid_tee;
    pjmedia_video_format_detail *vfd;
    
    PJ_ASSERT_RETURN(vid_tee && vid_tee->info.signature==TEE_PORT_SIGN,
		     PJ_EINVAL);
    
    if (tee->dst_port_cnt >= tee->dst_port_maxcnt)
	return PJ_ETOOMANY;
    
    pj_bzero(&tee->tee_conv[tee->dst_port_cnt], sizeof(tee->tee_conv[0]));
    
    /* Check if we need to create a converter. */
    vfd = pjmedia_format_get_video_format_detail(&port->info.fmt, PJ_TRUE);
    if (vid_tee->info.fmt.id != port->info.fmt.id ||
        vfd->size.w != vid_tee->info.fmt.det.vid.size.w ||
	vfd->size.h != vid_tee->info.fmt.det.vid.size.h)
    {
        const pjmedia_video_format_info *vfi;
        pjmedia_video_apply_fmt_param vafp;
        pjmedia_conversion_param conv_param;
        pj_status_t status;

        vfi = pjmedia_get_video_format_info(NULL, port->info.fmt.id);
        if (vfi == NULL)
            return PJMEDIA_EBADFMT;

        pj_bzero(&vafp, sizeof(vafp));
        vafp.size = port->info.fmt.det.vid.size;
        status = vfi->apply_fmt(vfi, &vafp);
        if (status != PJ_SUCCESS)
            return status;
        
        realloc_buf(tee, (option & PJMEDIA_VID_TEE_DST_DO_IN_PLACE_PROC)?
                    2: 1, vafp.framebytes);
        
        pjmedia_format_copy(&conv_param.src, &vid_tee->info.fmt);
	pjmedia_format_copy(&conv_param.dst, &port->info.fmt);
        
        status = pjmedia_converter_create(
                     NULL, tee->pool, &conv_param,
                     &tee->tee_conv[tee->dst_port_cnt].conv);
        if (status != PJ_SUCCESS)
            return status;
        
        tee->tee_conv[tee->dst_port_cnt].conv_buf_size = vafp.framebytes;
    } else {
        realloc_buf(tee, (option & PJMEDIA_VID_TEE_DST_DO_IN_PLACE_PROC)?
                    1: 0, tee->buf_size);        
    }
    
    tee->dst_ports[tee->dst_port_cnt].dst = port;
    tee->dst_ports[tee->dst_port_cnt].option = option;
    ++tee->dst_port_cnt;
    
    return PJ_SUCCESS;
}


/*
 * Remove a destination media port from the video tee.
 */
PJ_DEF(pj_status_t) pjmedia_vid_tee_remove_dst_port(pjmedia_port *vid_tee,
						    pjmedia_port *port)
{
    vid_tee_port *tee = (vid_tee_port*)vid_tee;
    unsigned i;

    PJ_ASSERT_RETURN(vid_tee && vid_tee->info.signature==TEE_PORT_SIGN,
		     PJ_EINVAL);

    for (i = 0; i < tee->dst_port_cnt; ++i) {
	if (tee->dst_ports[i].dst == port) {
            if (tee->tee_conv[i].conv)
                pjmedia_converter_destroy(tee->tee_conv[i].conv);
            
	    pj_array_erase(tee->dst_ports, sizeof(tee->dst_ports[0]),
			   tee->dst_port_cnt, i);
            pj_array_erase(tee->tee_conv, sizeof(tee->tee_conv[0]),
			   tee->dst_port_cnt, i);
	    --tee->dst_port_cnt;
	    return PJ_SUCCESS;
	}
    }

    return PJ_ENOTFOUND;
}


static pj_status_t tee_put_frame(pjmedia_port *port, pjmedia_frame *frame)
{
    vid_tee_port *tee = (vid_tee_port*)port;
    unsigned i, j;
    const pj_uint8_t PUT_FRM_DONE = 1;

    pj_bzero(tee->put_frm_flag, tee->dst_port_cnt *
				sizeof(tee->put_frm_flag[0]));

    for (i = 0; i < tee->dst_port_cnt; ++i) {
	pjmedia_frame frame_ = *frame;

        if (tee->put_frm_flag[i])
            continue;
        
        if (tee->tee_conv[i].conv) {
            pj_status_t status;
            
            frame_.buf  = tee->buf[0];
            frame_.size = tee->tee_conv[i].conv_buf_size;
            status = pjmedia_converter_convert(tee->tee_conv[i].conv,
                                               frame, &frame_);
            if (status != PJ_SUCCESS) {
                PJ_LOG(3, (THIS_FILE,
			       "Failed to convert frame for destination"
                               " port %d (%.*s)", i,
                               tee->dst_ports[i].dst->info.name.slen,
                               tee->dst_ports[i].dst->info.name.ptr));
                continue;
            }
        }
        
        /* Find other destination ports which has the same format so
         * we don't need to do the same conversion twice.
         */
        for (j = i; j < tee->dst_port_cnt; ++j) {
            pjmedia_frame framep;
            
            if (tee->put_frm_flag[j] ||
                (tee->dst_ports[j].dst->info.fmt.id != 
                 tee->dst_ports[i].dst->info.fmt.id) ||
                (tee->dst_ports[j].dst->info.fmt.det.vid.size.w != 
                 tee->dst_ports[i].dst->info.fmt.det.vid.size.w) ||
                (tee->dst_ports[j].dst->info.fmt.det.vid.size.h != 
                 tee->dst_ports[i].dst->info.fmt.det.vid.size.h))
            {
                continue;
            }
            
            framep = frame_;
            /* For dst_ports that do in-place processing, we need to duplicate
             * the data source first.
             */
            if (tee->dst_ports[j].option & PJMEDIA_VID_TEE_DST_DO_IN_PLACE_PROC)
            {
                PJ_ASSERT_RETURN(tee->buf_size <= frame_.size, PJ_ETOOBIG);
                framep.buf = tee->buf[tee->buf_cnt-1];
                framep.size = frame_.size;
                pj_memcpy(framep.buf, frame_.buf, frame_.size);
            }

            /* Deliver the data */
            pjmedia_port_put_frame(tee->dst_ports[j].dst, &framep);
            tee->put_frm_flag[j] = PUT_FRM_DONE;
            
            if (!tee->tee_conv[i].conv)
                break;
        }
    }

    return PJ_SUCCESS;
}

static pj_status_t tee_get_frame(pjmedia_port *port, pjmedia_frame *frame)
{
    PJ_UNUSED_ARG(port);
    PJ_UNUSED_ARG(frame);

    pj_assert(!"Bug! Tee port get_frame() shouldn't be called.");

    return PJ_EBUG;
}

static pj_status_t tee_destroy(pjmedia_port *port)
{
    vid_tee_port *tee = (vid_tee_port*)port;

    PJ_ASSERT_RETURN(port && port->info.signature==TEE_PORT_SIGN, PJ_EINVAL);

    pj_pool_release(tee->pool);
    if (tee->buf_pool)
        pj_pool_release(tee->buf_pool);
                    
    pj_bzero(tee, sizeof(*tee));

    return PJ_SUCCESS;
}


#endif /* PJMEDIA_HAS_VIDEO */
