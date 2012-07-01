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
#ifndef __PJNATH_TURN_SOCK_H__
#define __PJNATH_TURN_SOCK_H__

/**
 * @file turn_sock.h
 * @brief TURN relay using UDP client as transport protocol
 */
#include <pjnath/turn_session.h>
#include <pj/sock_qos.h>


PJ_BEGIN_DECL


/* **************************************************************************/
/**
@addtogroup PJNATH_TURN_SOCK
@{

This is a ready to use object for relaying application data via a TURN server,
by managing all the operations in \ref turn_op_sec.

\section turnsock_using_sec Using TURN transport

This object provides a thin wrapper to the \ref PJNATH_TURN_SESSION, hence the
API is very much the same (apart from the obvious difference in the names).
Please see \ref PJNATH_TURN_SESSION for the documentation on how to use the
session.

\section turnsock_samples_sec Samples

The \ref turn_client_sample is a sample application to use the
\ref PJNATH_TURN_SOCK.

Also see <b>\ref samples_page</b> for other samples.

 */


/** 
 * Opaque declaration for TURN client.
 */
typedef struct pj_turn_sock pj_turn_sock;

/**
 * This structure contains callbacks that will be called by the TURN
 * transport.
 */
typedef struct pj_turn_sock_cb
{
    /**
     * Notification when incoming data has been received from the remote
     * peer via the TURN server. The data reported in this callback will
     * be the exact data as sent by the peer (e.g. the TURN encapsulation
     * such as Data Indication or ChannelData will be removed before this
     * function is called).
     *
     * @param turn_sock	    The TURN client transport.
     * @param data	    The data as received from the peer.    
     * @param data_len	    Length of the data.
     * @param peer_addr	    The peer address.
     * @param addr_len	    The length of the peer address.
     */
    void (*on_rx_data)(pj_turn_sock *turn_sock,
		       void *pkt,
		       unsigned pkt_len,
		       const pj_sockaddr_t *peer_addr,
		       unsigned addr_len);

    /**
     * Notification when TURN session state has changed. Application should
     * implement this callback to monitor the progress of the TURN session.
     *
     * @param turn_sock	    The TURN client transport.
     * @param old_state	    Previous state.
     * @param new_state	    Current state.
     */
    void (*on_state)(pj_turn_sock *turn_sock, 
		     pj_turn_state_t old_state,
		     pj_turn_state_t new_state);

} pj_turn_sock_cb;


/**
 * This structure describes options that can be specified when creating
 * the TURN socket. Application should call #pj_turn_sock_cfg_default()
 * to initialize this structure with its default values before using it.
 */
typedef struct pj_turn_sock_cfg
{
    /**
     * QoS traffic type to be set on this transport. When application wants
     * to apply QoS tagging to the transport, it's preferable to set this
     * field rather than \a qos_param fields since this is more portable.
     *
     * Default value is PJ_QOS_TYPE_BEST_EFFORT.
     */
    pj_qos_type qos_type;

    /**
     * Set the low level QoS parameters to the transport. This is a lower
     * level operation than setting the \a qos_type field and may not be
     * supported on all platforms.
     *
     * By default all settings in this structure are not set.
     */
    pj_qos_params qos_params;

    /**
     * Specify if STUN socket should ignore any errors when setting the QoS
     * traffic type/parameters.
     *
     * Default: PJ_TRUE
     */
    pj_bool_t qos_ignore_error;

} pj_turn_sock_cfg;


/**
 * Initialize pj_turn_sock_cfg structure with default values.
 */
PJ_DECL(void) pj_turn_sock_cfg_default(pj_turn_sock_cfg *cfg);


/**
 * Create a TURN transport instance with the specified address family and
 * connection type. Once TURN transport instance is created, application
 * must call pj_turn_sock_alloc() to allocate a relay address in the TURN
 * server.
 *
 * @param cfg		The STUN configuration which contains among other
 *			things the ioqueue and timer heap instance for
 *			the operation of this transport.
 * @param af		Address family of the client connection. Currently
 *			pj_AF_INET() and pj_AF_INET6() are supported.
 * @param conn_type	Connection type to the TURN server. Both TCP and
 *			UDP are supported.
 * @param cb		Callback to receive events from the TURN transport.
 * @param setting	Optional settings to be specified to the transport.
 *			If this parameter is NULL, default values will be
 *			used.
 * @param user_data	Arbitrary application data to be associated with
 *			this transport.
 * @param p_turn_sock	Pointer to receive the created instance of the
 *			TURN transport.
 *
 * @return		PJ_SUCCESS if the operation has been successful,
 *			or the appropriate error code on failure.
 */
PJ_DECL(pj_status_t) pj_turn_sock_create(pj_stun_config *cfg,
					 int af,
					 pj_turn_tp_type conn_type,
					 const pj_turn_sock_cb *cb,
					 const pj_turn_sock_cfg *setting,
					 void *user_data,
					 pj_turn_sock **p_turn_sock);

/**
 * Destroy the TURN transport instance. This will gracefully close the
 * connection between the client and the TURN server. Although this
 * function will return immediately, the TURN socket deletion may continue
 * in the background and the application may still get state changes
 * notifications from this transport.
 *
 * @param turn_sock	The TURN transport instance.
 */
PJ_DECL(void) pj_turn_sock_destroy(pj_turn_sock *turn_sock);


/**
 * Associate a user data with this TURN transport. The user data may then
 * be retrieved later with #pj_turn_sock_get_user_data().
 *
 * @param turn_sock	The TURN transport instance.
 * @param user_data	Arbitrary data.
 *
 * @return		PJ_SUCCESS if the operation has been successful,
 *			or the appropriate error code on failure.
 */
PJ_DECL(pj_status_t) pj_turn_sock_set_user_data(pj_turn_sock *turn_sock,
					        void *user_data);

/**
 * Retrieve the previously assigned user data associated with this TURN
 * transport.
 *
 * @param turn_sock	The TURN transport instance.
 *
 * @return		The user/application data.
 */
PJ_DECL(void*) pj_turn_sock_get_user_data(pj_turn_sock *turn_sock);


/**
 * Get the TURN transport info. The transport info contains, among other
 * things, the allocated relay address.
 *
 * @param turn_sock	The TURN transport instance.
 * @param info		Pointer to be filled with TURN transport info.
 *
 * @return		PJ_SUCCESS if the operation has been successful,
 *			or the appropriate error code on failure.
 */
PJ_DECL(pj_status_t) pj_turn_sock_get_info(pj_turn_sock *turn_sock,
					   pj_turn_session_info *info);

/**
 * Acquire the internal mutex of the TURN transport. Application may need
 * to call this function to synchronize access to other objects alongside 
 * the TURN transport, to avoid deadlock.
 *
 * @param turn_sock	The TURN transport instance.
 *
 * @return		PJ_SUCCESS if the operation has been successful,
 *			or the appropriate error code on failure.
 */
PJ_DECL(pj_status_t) pj_turn_sock_lock(pj_turn_sock *turn_sock);


/**
 * Release the internal mutex previously held with pj_turn_sock_lock().
 *
 * @param turn_sock	The TURN transport instance.
 *
 * @return		PJ_SUCCESS if the operation has been successful,
 *			or the appropriate error code on failure.
 */
PJ_DECL(pj_status_t) pj_turn_sock_unlock(pj_turn_sock *turn_sock);


/**
 * Set STUN message logging for this TURN session. 
 * See #pj_stun_session_set_log().
 *
 * @param turn_sock	The TURN transport instance.
 * @param flags		Bitmask combination of #pj_stun_sess_msg_log_flag
 */
PJ_DECL(void) pj_turn_sock_set_log(pj_turn_sock *turn_sock,
				   unsigned flags);

/**
 * Configure the SOFTWARE name to be sent in all STUN requests by the
 * TURN session.
 *
 * @param turn_sock	The TURN transport instance.
 * @param sw	    Software name string. If this argument is NULL or
 *		    empty, the session will not include SOFTWARE attribute
 *		    in STUN requests and responses.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_turn_sock_set_software_name(pj_turn_sock *turn_sock,
						    const pj_str_t *sw);


/**
 * Allocate a relay address/resource in the TURN server. This function
 * will resolve the TURN server using DNS SRV (if desired) and send TURN
 * \a Allocate request using the specified credential to allocate a relay
 * address in the server. This function completes asynchronously, and
 * application will be notified when the allocation process has been
 * successful in the \a on_state() callback when the state is set to
 * PJ_TURN_STATE_READY. If the allocation fails, the state will be set
 * to PJ_TURN_STATE_DEALLOCATING or greater.
 *
 * @param turn_sock	The TURN transport instance.
 * @param domain	The domain, hostname, or IP address of the TURN
 *			server. When this parameter contains domain name,
 *			the \a resolver parameter must be set to activate
 *			DNS SRV resolution.
 * @param default_port	The default TURN port number to use when DNS SRV
 *			resolution is not used. If DNS SRV resolution is
 *			used, the server port number will be set from the
 *			DNS SRV records.
 * @param resolver	If this parameter is not NULL, then the \a domain
 *			parameter will be first resolved with DNS SRV and
 *			then fallback to using DNS A/AAAA resolution when
 *			DNS SRV resolution fails. If this parameter is
 *			NULL, the \a domain parameter will be resolved as
 *			hostname.
 * @param cred		The STUN credential to be used for the TURN server.
 * @param param		Optional TURN allocation parameter.
 *
 * @return		PJ_SUCCESS if the operation has been successfully
 *			queued, or the appropriate error code on failure.
 *			When this function returns PJ_SUCCESS, the final
 *			result of the allocation process will be notified
 *			to application in \a on_state() callback.
 *			
 */
PJ_DECL(pj_status_t) pj_turn_sock_alloc(pj_turn_sock *turn_sock,
				        const pj_str_t *domain,
				        int default_port,
				        pj_dns_resolver *resolver,
				        const pj_stun_auth_cred *cred,
				        const pj_turn_alloc_param *param);

/**
 * Create or renew permission in the TURN server for the specified peer IP
 * addresses. Application must install permission for a particular (peer)
 * IP address before it sends any data to that IP address, or otherwise
 * the TURN server will drop the data.
 *
 * @param turn_sock	The TURN transport instance.
 * @param addr_cnt	Number of IP addresses.
 * @param addr		Array of peer IP addresses. Only the address family
 *			and IP address portion of the socket address matter.
 * @param options	Specify 1 to let the TURN client session automatically
 *			renew the permission later when they are about to
 *			expire.
 *
 * @return		PJ_SUCCESS if the operation has been successfully
 *			issued, or the appropriate error code. Note that
 *			the operation itself will complete asynchronously.
 */
PJ_DECL(pj_status_t) pj_turn_sock_set_perm(pj_turn_sock *turn_sock,
					   unsigned addr_cnt,
					   const pj_sockaddr addr[],
					   unsigned options);

/**
 * Send a data to the specified peer address via the TURN relay. This 
 * function will encapsulate the data as STUN Send Indication or TURN
 * ChannelData packet and send the message to the TURN server. The TURN
 * server then will send the data to the peer.
 *
 * The allocation (pj_turn_sock_alloc()) must have been successfully
 * created before application can relay any data.
 *
 * @param turn_sock	The TURN transport instance.
 * @param pkt		The data/packet to be sent to peer.
 * @param pkt_len	Length of the data.
 * @param peer_addr	The remote peer address (the ultimate destination
 *			of the data, and not the TURN server address).
 * @param addr_len	Length of the address.
 *
 * @return		PJ_SUCCESS if the operation has been successful,
 *			or the appropriate error code on failure.
 */ 
PJ_DECL(pj_status_t) pj_turn_sock_sendto(pj_turn_sock *turn_sock,
					const pj_uint8_t *pkt,
					unsigned pkt_len,
					const pj_sockaddr_t *peer_addr,
					unsigned addr_len);

/**
 * Optionally establish channel binding for the specified a peer address.
 * This function will assign a unique channel number for the peer address
 * and request channel binding to the TURN server for this address. When
 * a channel has been bound to a peer, the TURN transport and TURN server
 * will exchange data using ChannelData encapsulation format, which has
 * lower bandwidth overhead than Send Indication (the default format used
 * when peer address is not bound to a channel).
 *
 * @param turn_sock	The TURN transport instance.
 * @param peer		The remote peer address.
 * @param addr_len	Length of the address.
 *
 * @return		PJ_SUCCESS if the operation has been successful,
 *			or the appropriate error code on failure.
 */
PJ_DECL(pj_status_t) pj_turn_sock_bind_channel(pj_turn_sock *turn_sock,
					       const pj_sockaddr_t *peer,
					       unsigned addr_len);


/**
 * @}
 */


PJ_END_DECL


#endif	/* __PJNATH_TURN_SOCK_H__ */

