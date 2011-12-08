/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
 *
 * Luigi Rizzo
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

/*! \file
 * \brief Wrapper for network related headers,
 * masking differences between various operating systems.
 * On passing, we also provide here trivial functions or
 * other simple wrappers to network-related functions.
 */

#ifndef _ASTERISK_NETWORK_H
#define _ASTERISK_NETWORK_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*
 * Include relevant network headers.
 * Our preferred choice are the standard BSD/linux/unix headers.
 * Missing them (e.g. for solaris or various windows environments),
 * we resort to whatever we find around, and provide local definitions
 * for the missing bits.
 */
#ifdef HAVE_ARPA_INET_H
#include <netinet/in.h>
#include <arpa/inet.h>		/* include early to override inet_ntoa */
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>
#elif defined(HAVE_WINSOCK_H)
#include <winsock.h>
typedef int socklen_t;
#elif defined(HAVE_WINSOCK2_H)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#error "don't know how to handle network functions here."
#endif

#ifndef HAVE_INET_ATON
int inet_aton(const char *cp, struct in_addr *pin);
#endif

#ifndef IFNAMSIZ
#define	IFNAMSIZ	16
#endif

#ifndef MAXHOSTNAMELEN
#define	MAXHOSTNAMELEN	256
#endif

/*!
 * \brief thread-safe replacement for inet_ntoa().
 *
 * \note It is very important to note that even though this is a thread-safe
 *       replacement for inet_ntoa(), it is *not* reentrant.  In a single
 *       thread, the result from a previous call to this function is no longer
 *       valid once it is called again.  If the result from multiple calls to
 *       this function need to be kept or used at once, then the result must be
 *       copied to a local buffer before calling this function again.
 */
const char *ast_inet_ntoa(struct in_addr ia);

#ifdef inet_ntoa
#undef inet_ntoa
#endif
#define inet_ntoa __dont__use__inet_ntoa__use__ast_inet_ntoa__instead__

/*! \brief Compares the source address and port of two sockaddr_in */
static force_inline int inaddrcmp(const struct sockaddr_in *sin1, const struct sockaddr_in *sin2)
{
        return ((sin1->sin_addr.s_addr != sin2->sin_addr.s_addr)
                || (sin1->sin_port != sin2->sin_port));
}

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_NETWORK_H */
