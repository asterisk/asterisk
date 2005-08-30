/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 * DNS SRV record support
 */

#ifndef _ASTERISK_SRV_H
#define _ASTERISK_SRV_H

struct ast_channel;

/*!
  \file srv.h
  \brief Support for DNS SRV records, used in to locate SIP services.
	Note: The Asterisk DNS SRV record support is broken, it only
	supports the first DNS SRV record and will give no load 
	balancing or failover support.
*/

/*! Lookup entry in SRV records Returns 1 if found, 0 if not found, -1 on hangup 
	Only do SRV record lookup if you get a domain without a port. If you get a port #, it's a DNS host name.
*/
/*!	\param	chan Ast channel
	\param	host host name (return value)
	\param	hostlen Length of string "host"
	\param	port Port number (return value)
	\param service Service tag for SRV lookup (like "_sip._udp" or "_stun._udp"
*/
extern int ast_get_srv(struct ast_channel *chan, char *host, int hostlen, int *port, const char *service);

#endif /* _ASTERISK_SRV_H */
