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

#include <pjmedia/stereo.h>
#include <pj/assert.h>
#include <pj/pool.h>
#include <pj/string.h>


#define SIGNATURE		PJMEDIA_SIG_PORT_STEREO


struct stereo_port
{
    pjmedia_port	 base;
    pjmedia_port	*dn_port;
    unsigned		 options;
    pj_int16_t		*put_buf;
    pj_int16_t		*get_buf;
};



static pj_status_t stereo_put_frame(pjmedia_port *this_port,
				    pjmedia_frame *frame);
static pj_status_t stereo_get_frame(pjmedia_port *this_port, 
				    pjmedia_frame *frame);
static pj_status_t stereo_destroy(pjmedia_port *this_port);



PJ_DEF(pj_status_t) pjmedia_stereo_port_create( pj_pool_t *pool,
						pjmedia_port *dn_port,
						unsigned channel_count,
						unsigned options,
						pjmedia_port **p_port )
{
    const pj_str_t name = pj_str("stereo");
    struct stereo_port *sport;
    unsigned samples_per_frame;

    /* Validate arguments. */
    PJ_ASSERT_RETURN(pool && dn_port && channel_count && p_port, PJ_EINVAL);

    /* Only supports 16bit samples per frame */
    PJ_ASSERT_RETURN(PJMEDIA_PIA_BITS(&dn_port->info) == 16,
		     PJMEDIA_ENCBITS);

    /* Validate channel counts */
    PJ_ASSERT_RETURN(((PJMEDIA_PIA_CCNT(&dn_port->info)>1 &&
			      channel_count==1) ||
		      (PJMEDIA_PIA_CCNT(&dn_port->info)==1 &&
			      channel_count>1)),
		      PJ_EINVAL);

    /* Create and initialize port. */
    sport = PJ_POOL_ZALLOC_T(pool, struct stereo_port);
    PJ_ASSERT_RETURN(sport != NULL, PJ_ENOMEM);

    samples_per_frame = PJMEDIA_PIA_SPF(&dn_port->info) * channel_count /
	                  PJMEDIA_PIA_CCNT(&dn_port->info);

    pjmedia_port_info_init(&sport->base.info, &name, SIGNATURE, 
	                   PJMEDIA_PIA_SRATE(&dn_port->info),
			   channel_count, 
			   PJMEDIA_PIA_BITS(&dn_port->info),
			   samples_per_frame);

    sport->dn_port = dn_port;
    sport->options = options;

    /* We always need buffer for put_frame */
    sport->put_buf = (pj_int16_t*)
		     pj_pool_alloc(pool,
				   PJMEDIA_PIA_AVG_FSZ(&dn_port->info));

    /* See if we need buffer for get_frame */
    if (PJMEDIA_PIA_CCNT(&dn_port->info) > channel_count) {
	sport->get_buf = (pj_int16_t*)
			 pj_pool_alloc(pool,
				       PJMEDIA_PIA_AVG_FSZ(&dn_port->info));
    }

    /* Media port interface */
    sport->base.get_frame = &stereo_get_frame;
    sport->base.put_frame = &stereo_put_frame;
    sport->base.on_destroy = &stereo_destroy;


    /* Done */
    *p_port = &sport->base;

    return PJ_SUCCESS;
}

static pj_status_t stereo_put_frame(pjmedia_port *this_port,
				    pjmedia_frame *frame)
{
    struct stereo_port *sport = (struct stereo_port*) this_port;
    const pjmedia_audio_format_detail *s_afd, *dn_afd;
    pjmedia_frame tmp_frame;

    /* Return if we don't have downstream port. */
    if (sport->dn_port == NULL) {
	return PJ_SUCCESS;
    }

    s_afd = pjmedia_format_get_audio_format_detail(&this_port->info.fmt, 1);
    dn_afd = pjmedia_format_get_audio_format_detail(&sport->dn_port->info.fmt,
						    1);

    if (frame->type == PJMEDIA_FRAME_TYPE_AUDIO) {
	tmp_frame.buf = sport->put_buf;
	if (dn_afd->channel_count == 1) {
	    pjmedia_convert_channel_nto1((pj_int16_t*)tmp_frame.buf, 
					 (const pj_int16_t*)frame->buf,
					 s_afd->channel_count,
					 PJMEDIA_AFD_SPF(s_afd),
					 (sport->options & PJMEDIA_STEREO_MIX),
					 0);
	} else {
	    pjmedia_convert_channel_1ton((pj_int16_t*)tmp_frame.buf, 
					 (const pj_int16_t*)frame->buf,
					 dn_afd->channel_count,
					 PJMEDIA_AFD_SPF(s_afd),
					 sport->options);
	}
	tmp_frame.size = PJMEDIA_AFD_AVG_FSZ(dn_afd);
    } else {
	tmp_frame.buf = frame->buf;
	tmp_frame.size = frame->size;
    }

    tmp_frame.type = frame->type;
    tmp_frame.timestamp.u64 = frame->timestamp.u64;

    return pjmedia_port_put_frame( sport->dn_port, &tmp_frame );
}



static pj_status_t stereo_get_frame(pjmedia_port *this_port, 
				    pjmedia_frame *frame)
{
    struct stereo_port *sport = (struct stereo_port*) this_port;
    const pjmedia_audio_format_detail *s_afd, *dn_afd;
    pjmedia_frame tmp_frame;
    pj_status_t status;

    /* Return silence if we don't have downstream port */
    if (sport->dn_port == NULL) {
	pj_bzero(frame->buf, frame->size);
	return PJ_SUCCESS;
    }

    s_afd = pjmedia_format_get_audio_format_detail(&this_port->info.fmt, 1);
    dn_afd = pjmedia_format_get_audio_format_detail(&sport->dn_port->info.fmt,
						    1);

    tmp_frame.buf = sport->get_buf? sport->get_buf : frame->buf;
    tmp_frame.size = PJMEDIA_PIA_AVG_FSZ(&sport->dn_port->info);
    tmp_frame.timestamp.u64 = frame->timestamp.u64;
    tmp_frame.type = PJMEDIA_FRAME_TYPE_AUDIO;

    status = pjmedia_port_get_frame( sport->dn_port, &tmp_frame);
    if (status != PJ_SUCCESS)
	return status;

    if (tmp_frame.type != PJMEDIA_FRAME_TYPE_AUDIO) {
	frame->type = tmp_frame.type;
	frame->timestamp = tmp_frame.timestamp;
	frame->size = tmp_frame.size;
	if (tmp_frame.size && tmp_frame.buf == sport->get_buf)
	    pj_memcpy(frame->buf, tmp_frame.buf, tmp_frame.size);
	return PJ_SUCCESS;
    }

    if (s_afd->channel_count == 1) {
	pjmedia_convert_channel_nto1((pj_int16_t*)frame->buf, 
				     (const pj_int16_t*)tmp_frame.buf,
				     dn_afd->channel_count,
				     PJMEDIA_AFD_SPF(s_afd),
				     (sport->options & PJMEDIA_STEREO_MIX), 0);
    } else {
	pjmedia_convert_channel_1ton((pj_int16_t*)frame->buf, 
				     (const pj_int16_t*)tmp_frame.buf,
				     s_afd->channel_count,
				     PJMEDIA_AFD_SPF(dn_afd),
				     sport->options);
    }

    frame->size = PJMEDIA_AFD_AVG_FSZ(s_afd);
    frame->type = PJMEDIA_FRAME_TYPE_AUDIO;

    return PJ_SUCCESS;
}


static pj_status_t stereo_destroy(pjmedia_port *this_port)
{
    struct stereo_port *sport = (struct stereo_port*) this_port;

    if ((sport->options & PJMEDIA_STEREO_DONT_DESTROY_DN)==0) {
	pjmedia_port_destroy(sport->dn_port);
	sport->dn_port = NULL;
    }

    return PJ_SUCCESS;
}

