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
#ifndef __PJMEDIA_TRANSPORT_ICE_H__
#define __PJMEDIA_TRANSPORT_ICE_H__


/**
 * @file transport_ice.h
 * @brief ICE capable media transport.
 */

#include <pjmedia/stream.h>
#include <pjnath/ice_strans.h>


/**
 * @defgroup PJMEDIA_TRANSPORT_ICE ICE Media Transport 
 * @ingroup PJMEDIA_TRANSPORT
 * @brief Interactive Connectivity Establishment (ICE) transport
 * @{
 *
 * This describes the implementation of media transport using
 * Interactive Connectivity Establishment (ICE) protocol.
 */

PJ_BEGIN_DECL


/**
 * Structure containing callbacks to receive ICE notifications.
 */
typedef struct pjmedia_ice_cb
{
    /**
     * This callback will be called when ICE negotiation completes.
     *
     * @param tp	PJMEDIA ICE transport.
     * @param op	The operation
     * @param status	Operation status.
     */
    void    (*on_ice_complete)(pjmedia_transport *tp,
			       pj_ice_strans_op op,
			       pj_status_t status);

} pjmedia_ice_cb;


/**
 * This structure specifies ICE transport specific info. This structure
 * will be filled in media transport specific info.
 */
typedef struct pjmedia_ice_transport_info
{
    /**
     * ICE sesion state.
     */
    pj_ice_strans_state sess_state;

    /**
     * Session role.
     */
    pj_ice_sess_role role;

    /**
     * Number of components in the component array. Before ICE negotiation
     * is complete, the number represents the number of components of the
     * local agent. After ICE negotiation has been completed successfully,
     * the number represents the number of common components between local
     * and remote agents.
     */
    unsigned comp_cnt;

    /**
     * Array of ICE components. Typically the first element denotes RTP and
     * second element denotes RTCP.
     */
    struct
    {
	/**
	 * Local candidate type.
	 */
	pj_ice_cand_type    lcand_type;

	/**
	 * The local address.
	 */
	pj_sockaddr	    lcand_addr;

	/**
	 * Remote candidate type.
	 */
	pj_ice_cand_type    rcand_type;

	/**
	 * Remote address.
	 */
	pj_sockaddr	    rcand_addr;

    } comp[2];

} pjmedia_ice_transport_info;


/**
 * Options that can be specified when creating ICE transport.
 */
enum pjmedia_transport_ice_options
{
    /**
     * Normally when remote doesn't use ICE, the ICE transport will 
     * continuously check the source address of incoming packets to see 
     * if it is different than the configured remote address, and switch 
     * the remote address to the source address of the packet if they 
     * are different after several packets are received.
     * Specifying this option will disable this feature.
     */
    PJMEDIA_ICE_NO_SRC_ADDR_CHECKING = 1
};


/**
 * Create the Interactive Connectivity Establishment (ICE) media transport
 * using the specified configuration. When STUN or TURN (or both) is used,
 * the creation operation will complete asynchronously, when STUN resolution
 * and TURN allocation completes. When the initialization completes, the
 * \a on_ice_complete() complete will be called with \a op parameter equal
 * to PJ_ICE_STRANS_OP_INIT.
 *
 * In addition, this transport will also notify the application about the
 * result of ICE negotiation, also in \a on_ice_complete() callback. In this
 * case the callback will be called with \a op parameter equal to
 * PJ_ICE_STRANS_OP_NEGOTIATION.
 *
 * Other than this, application should use the \ref PJMEDIA_TRANSPORT API
 * to manipulate this media transport.
 *
 * @param endpt		The media endpoint.
 * @param name		Optional name to identify this ICE media transport
 *			for logging purposes.
 * @param comp_cnt	Number of components to be created.
 * @param cfg		Pointer to configuration settings.
 * @param cb		Optional structure containing ICE specific callbacks.
 * @param p_tp		Pointer to receive the media transport instance.
 *
 * @return		PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_ice_create(pjmedia_endpt *endpt,
					const char *name,
					unsigned comp_cnt,
					const pj_ice_strans_cfg *cfg,
					const pjmedia_ice_cb *cb,
					pjmedia_transport **p_tp);


/**
 * The same as #pjmedia_ice_create() with additional \a options param.
 *
 * @param endpt		The media endpoint.
 * @param name		Optional name to identify this ICE media transport
 *			for logging purposes.
 * @param comp_cnt	Number of components to be created.
 * @param cfg		Pointer to configuration settings.
 * @param cb		Optional structure containing ICE specific callbacks.
 * @param options	Options, see #pjmedia_transport_ice_options.
 * @param p_tp		Pointer to receive the media transport instance.
 *
 * @return		PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_ice_create2(pjmedia_endpt *endpt,
					 const char *name,
					 unsigned comp_cnt,
					 const pj_ice_strans_cfg *cfg,
					 const pjmedia_ice_cb *cb,
					 unsigned options,
					 pjmedia_transport **p_tp);

/**
 * The same as #pjmedia_ice_create2() with additional \a user_data param.
 *
 * @param endpt		The media endpoint.
 * @param name		Optional name to identify this ICE media transport
 *			for logging purposes.
 * @param comp_cnt	Number of components to be created.
 * @param cfg		Pointer to configuration settings.
 * @param cb		Optional structure containing ICE specific callbacks.
 * @param options	Options, see #pjmedia_transport_ice_options.
 * @param user_data	User data to be attached to the transport.
 * @param p_tp		Pointer to receive the media transport instance.
 *
 * @return		PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_ice_create3(pjmedia_endpt *endpt,
					 const char *name,
					 unsigned comp_cnt,
					 const pj_ice_strans_cfg *cfg,
					 const pjmedia_ice_cb *cb,
					 unsigned options,
					 void *user_data,
					 pjmedia_transport **p_tp);

PJ_END_DECL


/**
 * @}
 */


#endif	/* __PJMEDIA_TRANSPORT_ICE_H__ */


