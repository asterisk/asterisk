/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
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

/*!
 * \file stun.h
 * \brief STUN support.
 *
 * STUN is defined in RFC 3489.
 */

#ifndef _ASTERISK_STUN_H
#define _ASTERISK_STUN_H

#include "asterisk/network.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static const int STANDARD_STUN_PORT = 3478;

enum ast_stun_result {
	AST_STUN_IGNORE = 0,
	AST_STUN_ACCEPT,
};

struct stun_attr;

/*! \brief Generic STUN request
 * send a generic stun request to the server specified.
 * \param s the socket used to send the request
 * \param dst the address of the STUN server
 * \param username if non null, add the username in the request
 * \param answer if non null, the function waits for a response and
 *    puts here the externally visible address.
 * \return 0 on success, other values on error.
 * The interface it may change in the future.
 */
int ast_stun_request(int s, struct sockaddr_in *dst, const char *username, struct sockaddr_in *answer);

/*! \brief callback type to be invoked on stun responses. */
typedef int (stun_cb_f)(struct stun_attr *attr, void *arg);

/*! \brief handle an incoming STUN message.
 *
 * Do some basic sanity checks on packet size and content,
 * try to extract a bit of information, and possibly reply.
 * At the moment this only processes BIND requests, and returns
 * the externally visible address of the request.
 * If a callback is specified, invoke it with the attribute.
 */
int ast_stun_handle_packet(int s, struct sockaddr_in *src, unsigned char *data, size_t len, stun_cb_f *stun_cb, void *arg);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_STUN_H */
