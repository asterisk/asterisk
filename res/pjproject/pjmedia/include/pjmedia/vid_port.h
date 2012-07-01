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
#ifndef __PJMEDIA_VIDPORT_H__
#define __PJMEDIA_VIDPORT_H__

/**
 * @file pjmedia/videoport.h Video media port
 * @brief Video media port
 */

#include <pjmedia-videodev/videodev.h>
#include <pjmedia/port.h>

/**
 * @defgroup PJMEDIA_VIDEO_PORT Video media port
 * @ingroup PJMEDIA_PORT_CLOCK
 * @brief Video media port
 * @{
 */

PJ_BEGIN_DECL

/**
 * This structure describes the parameters to create a video port
 */
typedef struct pjmedia_vid_port_param
{
    /**
     * Video stream parameter.
     */
    pjmedia_vid_dev_param	vidparam;

    /**
     * Specify whether the video port should use active or passive interface.
     * If active interface is selected, the video port will perform as
     * a media clock, automatically calls pjmedia_port_get_frame() and
     * pjmedia_port_put_frame() of its slave port (depending on the direction
     * that is specified when opening the video stream). If passive interface
     * is selected, application can retrieve the media port of this video
     * port by calling pjmedia_vid_port_get_passive_port(), and subsequently
     * calls pjmedia_port_put_frame() or pjmedia_port_get_frame() to that
     * media port.
     *
     * Default: PJ_TRUE
     */
    pj_bool_t		active;

} pjmedia_vid_port_param;

/**
 * Opaque data type for video port.
 */
typedef struct pjmedia_vid_port pjmedia_vid_port;

/**
 * Initialize the parameter with the default values. Note that this typically
 * would only fill the structure to zeroes unless they have different default
 * values.
 *
 * @param prm	The parameter.
 */
PJ_DECL(void) pjmedia_vid_port_param_default(pjmedia_vid_port_param *prm);

/**
 * Create a video port with the specified parameter.
 *
 * @param pool		Pool to allocate memory from.
 * @param prm		The video port parameter.
 * @param p_vp		Pointer to receive the result.
 *
 * @return		PJ_SUCCESS if video port has been created
 * 			successfully, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_port_create(pj_pool_t *pool,
					     const pjmedia_vid_port_param *prm,
					     pjmedia_vid_port **p_vp);

/**
 * Set the callbacks of the video port's underlying video stream.
 *
 * @param vid_port	The video port.
 * @param cb            Pointer to structure containing video stream
 *                      callbacks.
 * @param user_data     Arbitrary user data, which will be given back in the
 *                      callbacks.
 */
PJ_DECL(void) pjmedia_vid_port_set_cb(pjmedia_vid_port *vid_port,
				      const pjmedia_vid_dev_cb *cb,
                                      void *user_data);

/**
 * Return the underlying video stream of the video port.
 *
 * @param vid_port	The video port.
 *
 * @return		The video stream.
 */
PJ_DECL(pjmedia_vid_dev_stream*)
pjmedia_vid_port_get_stream(pjmedia_vid_port *vid_port);

/**
 * Return the (passive) media port of the video port. This operation
 * is only valid for video ports created with passive interface selected.
 * Retrieving the media port for active video ports may raise an
 * assertion.
 *
 *  @param vid_port	The video port.
 *
 *  @return		The media port instance, or NULL.
 */
PJ_DECL(pjmedia_port*)
pjmedia_vid_port_get_passive_port(pjmedia_vid_port *vid_port);

/**
 * Get a clock source from the video port.
 *
 * @param vid_port  The video port.
 *
 * @return	    The clock source.
 */
PJ_DECL(pjmedia_clock_src *)
pjmedia_vid_port_get_clock_src( pjmedia_vid_port *vid_port );

/**
 * Set a clock source for the video port.
 *
 * @param vid_port  The video port.
 * @param clocksrc  The clock source.
 *
 * @return	    PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t)
pjmedia_vid_port_set_clock_src( pjmedia_vid_port *vid_port,
                                pjmedia_clock_src *clocksrc );

/**
 * Connect the video port to a downstream (slave) media port. This operation
 * is only valid for video ports created with active interface selected.
 * Connecting a passive video port may raise an assertion.
 *
 * @param vid_port	The video port.
 * @param port		A downstream media port to be connected to
 * 			this video port.
 * @param destroy	Specify if the downstream media port should also be
 * 			destroyed by this video port when the video port
 * 			is destroyed.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_port_connect(pjmedia_vid_port *vid_port,
					      pjmedia_port *port,
					      pj_bool_t destroy);

/**
 * Disconnect the video port from its downstream (slave) media port, if any.
 * This operation is only valid for video ports created with active interface
 * selected, and assertion may be triggered if this is invoked on a passive
 * video port.
 *
 * @param vid_port	The video port.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_port_disconnect(pjmedia_vid_port *vid_port);

/**
 * Retrieve the media port currently connected as downstream media port of the
 * specified video port. This operation is only valid for video ports created
 * with active interface selected, and assertion may be triggered if this is
 * invoked on a passive video port.
 *
 * @param vid_port	The video port.
 *
 * @return		Media port currently connected to the video port,
 * 			if any.
 */
PJ_DECL(pjmedia_port*)
pjmedia_vid_port_get_connected_port(pjmedia_vid_port *vid_port);

/**
 * Start the video port.
 *
 * @param vid_port	The video port.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_port_start(pjmedia_vid_port *vid_port);

/**
 * Query whether the video port has been started.
 *
 * @param vid_port	The video port.
 *
 * @return		PJ_TRUE if the video port has been started.
 */
PJ_DECL(pj_bool_t) pjmedia_vid_port_is_running(pjmedia_vid_port *vid_port);

/**
 * Stop the video port.
 *
 * @param vid_port	The video port.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_port_stop(pjmedia_vid_port *vid_port);

/**
 * Destroy the video port, along with its video stream. If the video port is
 * an active one, this may also destroy the downstream media port, if the
 * destroy flag is set when the media port is connected.
 *
 * @param vid_port	The video port.
 */
PJ_DECL(void) pjmedia_vid_port_destroy(pjmedia_vid_port *vid_port);


PJ_END_DECL

/**
 * @}
 */

#endif /* __PJMEDIA_VIDPORT_H__ */

