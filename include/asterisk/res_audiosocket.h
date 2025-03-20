/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2019, CyCore Systems, Inc
 *
 * Seán C McCord <scm@cycoresys.com
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
 * \brief AudioSocket support functions
 *
 * \author Seán C McCord <scm@cycoresys.com>
 *
 */

#ifndef _ASTERISK_RES_AUDIOSOCKET_H
#define _ASTERISK_RES_AUDIOSOCKET_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <uuid/uuid.h>

#include "asterisk/frame.h"
#include "asterisk/uuid.h"


enum ast_audiosocket_msg_kind {
	/*! \brief Message indicates the channel should be hung up, direction: Sent only. */
	AST_AUDIOSOCKET_KIND_HANGUP = 0x00,

	/*! \brief Message contains the connection's UUID, direction: Received only. */
	AST_AUDIOSOCKET_KIND_UUID   = 0x01,

	/*! \brief Message contains a DTMF digit, direction: Received only. */
	AST_AUDIOSOCKET_KIND_DTMF   = 0x03,

	/*! \brief Messages contains audio data, direction: Sent and received. */
	AST_AUDIOSOCKET_KIND_AUDIO  = 0x10,

	/*! \brief An Asterisk-side error occurred, direction: Received only. */
	AST_AUDIOSOCKET_KIND_ERROR  = 0xFF,
};


/*!
 * \brief Send the initial message to an AudioSocket server
 *
 * \param server The server address, including port.
 * \param chan An optional channel which will be put into autoservice during
 * the connection period.  If there is no channel to be autoserviced, pass NULL
 * instead.
 *
 * \retval socket file descriptor for AudioSocket on success
 * \retval -1 on error
 */
const int ast_audiosocket_connect(const char *server, struct ast_channel *chan);

/*!
 * \brief Send the initial message to an AudioSocket server
 *
 * \param svc The file descriptor of the network socket to the AudioSocket server.
 * \param id The UUID to send to the AudioSocket server to uniquely identify this connection.
 *
 * \retval 0 on success
 * \retval -1 on error
 */
const int ast_audiosocket_init(const int svc, const char *id);

/*!
 * \brief Send an Asterisk audio frame to an AudioSocket server
 *
 * \param svc The file descriptor of the network socket to the AudioSocket server.
 * \param f The Asterisk audio frame to send.
 *
 * \retval 0 on success
 * \retval -1 on error
 */
const int ast_audiosocket_send_frame(const int svc, const struct ast_frame *f);

/*!
 * \brief Receive an Asterisk frame from an AudioSocket server
 *
 * This returned object is a pointer to an Asterisk frame which must be
 * manually freed by the caller.
 *
 * \param svc The file descriptor of the network socket to the AudioSocket server.
 *
 * \retval A \ref ast_frame on success
 * \retval NULL on error
 */
struct ast_frame *ast_audiosocket_receive_frame(const int svc);

/*!
 * \brief Receive an Asterisk frame from an AudioSocket server
 *
 * This returned object is a pointer to an Asterisk frame which must be
 * manually freed by the caller.
 *
 * \param svc The file descriptor of the network socket to the AudioSocket
 * server.
 * \param hangup Will be true if the AudioSocket server requested the channel
 * be hung up, otherwise false. Used as an out-parameter only, pass NULL if
 * not needed. The function return value will always be NULL when true.
 *
 * \retval A \ref ast_frame on success
 * \retval NULL on error or when the hungup parameter is true.
 */
struct ast_frame *ast_audiosocket_receive_frame_with_hangup(const int svc,
	int *const hangup);

#endif /* _ASTERISK_RES_AUDIOSOCKET_H */
