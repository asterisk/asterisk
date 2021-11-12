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
 * \file
 *
 * \brief STUN support.
 *
 * STUN is defined in RFC 3489.
 */

#ifndef _ASTERISK_STUN_H
#define _ASTERISK_STUN_H

#include "asterisk/network.h"
#include "asterisk/logger_category.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* STUN debug logging category name */
#define AST_LOG_CATEGORY_STUN "stun"
/* STUN packet debug logging category name */
#define AST_LOG_CATEGORY_STUN_PACKET "stun_packet"

uintmax_t ast_debug_category_stun_id(void);
uintmax_t ast_debug_category_stun_packet_id(void);

#define AST_DEBUG_CATEGORY_STUN ast_debug_category_stun_id() /* STUN debug logging category id */
#define AST_DEBUG_CATEGORY_STUN_PACKET ast_debug_category_stun_packet_id() /* STUN packet debug logging category id */

/*!
 * \brief Log debug level STUN information
 *
 * \param sublevel Debug output sublevel (>= 0)
 * \param ... String format and any associated arguments
 */
#define ast_debug_stun(sublevel, ...) \
	ast_debug_category(sublevel, AST_DEBUG_CATEGORY_STUN, __VA_ARGS__)

/* Is logging of stun packets allowed? */
#define ast_debug_stun_packet_is_allowed \
	ast_debug_category_is_allowed(AST_LOG_CATEGORY_ENABLED, AST_DEBUG_CATEGORY_STUN_PACKET)

static const int STANDARD_STUN_PORT = 3478;

enum ast_stun_result {
	AST_STUN_IGNORE = 0,
	AST_STUN_ACCEPT,
};

struct stun_attr;

/*!
 * \brief Generic STUN request.
 *
 * \param s The socket used to send the request.
 * \param dst If non null, the address of the STUN server.
 *    Only needed if the socket is not bound or connected.
 * \param username If non null, add the username in the request.
 * \param answer If non null, the function waits for a response and
 *    puts here the externally visible address.
 *
 * \details
 * Send a generic STUN request to the server specified, possibly
 * waiting for a reply and filling the answer parameter with the
 * externally visible address.  Note that in this case the
 * request will be blocking.
 *
 * \note The interface may change slightly in the future.
 *
 * \retval 0 on success.
 * \retval <0 on error.
 * \retval >0 on timeout.
 */
int ast_stun_request(int s, struct sockaddr_in *dst, const char *username, struct sockaddr_in *answer);

/*! \brief callback type to be invoked on stun responses. */
typedef int (stun_cb_f)(struct stun_attr *attr, void *arg);

/*!
 * \brief handle an incoming STUN message.
 *
 * \param s Socket to send any response to.
 * \param src Address where packet came from.
 * \param data STUN packet buffer to process.
 * \param len Length of packet
 * \param stun_cb If not NULL, callback for each STUN attribute.
 * \param arg Arg to pass to callback.
 *
 * \details
 * Do some basic sanity checks on packet size and content,
 * try to extract a bit of information, and possibly reply.
 * At the moment this only processes BIND requests, and returns
 * the externally visible address of the request.
 * If a callback is specified, invoke it with the attribute.
 *
 * \retval AST_STUN_ACCEPT if responed to a STUN request
 * \retval AST_STUN_IGNORE
 * \retval -1 on error
 */
int ast_stun_handle_packet(int s, struct sockaddr_in *src, unsigned char *data, size_t len, stun_cb_f *stun_cb, void *arg);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_STUN_H */
