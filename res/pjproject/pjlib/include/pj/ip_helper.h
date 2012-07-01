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
#ifndef __PJ_IP_ROUTE_H__
#define __PJ_IP_ROUTE_H__

/**
 * @file ip_helper.h
 * @brief IP helper API
 */

#include <pj/sock.h>

PJ_BEGIN_DECL

/**
 * @defgroup pj_ip_helper IP Interface and Routing Helper
 * @ingroup PJ_IO
 * @{
 *
 * This module provides functions to query local host's IP interface and 
 * routing table.
 */

/**
 * This structure describes IP routing entry.
 */
typedef union pj_ip_route_entry
{
    /** IP routing entry for IP version 4 routing */
    struct
    {
	pj_in_addr	if_addr;    /**< Local interface IP address.	*/
	pj_in_addr	dst_addr;   /**< Destination IP address.	*/
	pj_in_addr	mask;	    /**< Destination mask.		*/
    } ipv4;
} pj_ip_route_entry;


/**
 * Enumerate the local IP interfaces currently active in the host.
 *
 * @param af	    Family of the address to be retrieved. Application
 *		    may specify pj_AF_UNSPEC() to retrieve all addresses,
 *		    or pj_AF_INET() or pj_AF_INET6() to retrieve interfaces
 *		    with specific address family.
 * @param count	    On input, specify the number of entries. On output,
 *		    it will be filled with the actual number of entries.
 * @param ifs	    Array of socket addresses, which address part will
 *		    be filled with the interface address. The address
 *		    family part will be initialized with the address
 *		    family of the IP address.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_enum_ip_interface(int af,
					  unsigned *count,
					  pj_sockaddr ifs[]);


/**
 * Enumerate the IP routing table for this host.
 *
 * @param count	    On input, specify the number of routes entries. On output,
 *		    it will be filled with the actual number of route entries.
 * @param routes    Array of IP routing entries.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_enum_ip_route(unsigned *count,
				      pj_ip_route_entry routes[]);



/** @} */

PJ_END_DECL


#endif	/* __PJ_IP_ROUTE_H__ */

