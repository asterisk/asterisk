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
#ifndef __PJMEDIA_VID_TEE_H__
#define __PJMEDIA_VID_TEE_H__

/**
 * @file vid_tee.h
 * @brief Video tee (source duplicator).
 */
#include <pjmedia/port.h>

/**
 * @addtogroup PJMEDIA_VID_TEE Video source duplicator
 * @ingroup PJMEDIA_PORT
 * @brief Duplicate video data from a media port into multiple media port 
 *  destinations
 * @{
 *
 * This section describes media port to duplicate video data in the stream.
 *
 * A video tee branches video stream flow from one source port to multiple
 * destination ports by simply duplicating the video data supplied by the
 * source port and delivering the copy to all registered destinations.
 *
 * The video tee is a unidirectional port, i.e: data flows from source port
 * to destination ports only. Also, the video source port MUST actively call
 * pjmedia_port_put_frame() to the video tee and the video destination ports
 * MUST NEVER call pjmedia_port_get_frame() to the video tee. Please note that
 * there is no specific order of which destination port will receive a frame
 * from the video tee.
 *
 * The video tee is not thread-safe, so it is application responsibility
 * to synchronize video tee operations, e.g: make sure the source port is
 * paused during adding or removing a destination port.
 */

PJ_BEGIN_DECL


/**
 * Enumeration of video tee flags.
 */
typedef enum pjmedia_vid_tee_flag
{
    /**
     * Tell the video tee that the destination port will do in-place
     * processing, so the delivered data may be modified by this port.
     * If this flag is used, buffer will be copied before being given to
     * the destination port.
     */
    PJMEDIA_VID_TEE_DST_DO_IN_PLACE_PROC    = 4,

} pjmedia_vid_tee_flag;


/**
 * Create a video tee port with the specified source media port. Application
 * should destroy the tee with pjmedia_port_destroy() as usual. Note that
 * destroying the tee does not destroy its destination ports.
 *
 * @param pool		    The pool.
 * @param fmt		    The source media port's format.
 * @param max_dst_cnt	    The maximum number of destination ports supported.
 * @param p_vid_tee	    Pointer to receive the video tee port.
 *
 * @return		    PJ_SUCCESS on success, or the appropriate
 *			    error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_tee_create(pj_pool_t *pool,
					    const pjmedia_format *fmt,
					    unsigned max_dst_cnt,
					    pjmedia_port **p_vid_tee);

/**
 * Add a destination media port to the video tee. For this function, the
 * destination port's media format must match the source format.
 *
 * @param vid_tee	    The video tee.
 * @param option	    Video tee option, see @pjmedia_vid_tee_flag.
 * @param port		    The destination media port.
 *
 * @return		    PJ_SUCCESS on success, or the appropriate error
 *			    code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_tee_add_dst_port(pjmedia_port *vid_tee,
						  unsigned option,
						  pjmedia_port *port);


/**
 * Add a destination media port to the video tee. This function will also
 * create a converter if the destination port's media format does not match
 * the source format.
 *
 * @param vid_tee	    The video tee.
 * @param option	    Video tee option, see @pjmedia_vid_tee_flag.
 * @param port		    The destination media port.
 *
 * @return		    PJ_SUCCESS on success, or the appropriate error
 *			    code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_tee_add_dst_port2(pjmedia_port *vid_tee,
                                                   unsigned option,
						   pjmedia_port *port);


/**
 * Remove a destination media port from the video tee.
 *
 * @param vid_tee	    The video tee.
 * @param port		    The destination media port to be removed.
 *
 * @return		    PJ_SUCCESS on success, or the appropriate error
 *			    code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_tee_remove_dst_port(pjmedia_port *vid_tee,
						     pjmedia_port *port);


PJ_END_DECL

/**
 * @}
 */

#endif /* __PJMEDIA_VID_TEE_H__ */
