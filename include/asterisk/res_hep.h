/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2014, Digium, Inc.
 *
 * Alexandr Dubovikov <alexandr.dubovikov@sipcapture.org>
 * Matt Jordan <mjordan@digium.com>
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
 * \brief Routines for integration with Homer using HEPv3
 *
 * \author Alexandr Dubovikov <alexandr.dubovikov@sipcapture.org>
 * \author Matt Jordan <mjordan@digium.com>
 *
 */

#ifndef _ASTERISK_RES_HEPV3_H
#define _ASTERISK_RES_HEPV3_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/netsock2.h"

/*! \brief HEPv3 Packet Capture Types */
enum hepv3_capture_type {
	HEPV3_CAPTURE_TYPE_SIP    = 0x01,
	HEPV3_CAPTURE_TYPE_H323   = 0x02,
	HEPV3_CAPTURE_TYPE_SDP    = 0x03,
	HEPV3_CAPTURE_TYPE_RTP    = 0x04,
	HEPV3_CAPTURE_TYPE_RTCP   = 0x05,
	HEPV3_CAPTURE_TYPE_MGCP   = 0x06,
	HEPV3_CAPTURE_TYPE_MEGACO = 0x07,
	HEPV3_CAPTURE_TYPE_M2UA   = 0x08,
	HEPV3_CAPTURE_TYPE_M3UA   = 0x09,
	HEPV3_CAPTURE_TYPE_IAX    = 0x10,
};

enum hep_uuid_type {
	HEP_UUID_TYPE_CALL_ID = 0,
	HEP_UUID_TYPE_CHANNEL,
};

/*! \brief HEPv3 Capture Info */
struct hepv3_capture_info {
	/*! The source address of the packet */
	struct ast_sockaddr src_addr;
	/*! The destination address of the packet */
	struct ast_sockaddr dst_addr;
	/*! The time the packet was captured */
	struct timeval capture_time;
	/*! The actual payload */
	void *payload;
	/*! Some UUID for the packet */
	char *uuid;
	/*! The \ref hepv3_capture_type packet type captured */
	enum hepv3_capture_type capture_type;
	/*! The size of the payload */
	size_t len;
	/*! If non-zero, the payload accompanying this capture info will be compressed */
	unsigned int zipped:1;
	/*! The IPPROTO_* protocol where we captured the packet */
	int protocol_id;
};

/*!
 * \brief Create a \ref hepv3_capture_info object
 *
 * This returned object is an ao2 reference counted object.
 *
 * Any attribute in the returned \ref hepv3_capture_info that is a
 * pointer should point to something that is allocated on the heap,
 * as it will be free'd when the \ref hepv3_capture_info object is
 * reclaimed.
 *
 * \param payload The payload to send to the HEP capture node
 * \param len     Length of \p payload
 *
 * \return A \ref hepv3_capture_info ref counted object on success
 * \retval NULL on error
 */
struct hepv3_capture_info *hepv3_create_capture_info(const void *payload, size_t len);

/*!
 * \brief Send a generic packet capture to HEPv3
 *
 * \param capture_info Information describing the packet. This
 * should be a reference counted object, created via
 * \ref hepv3_create_capture_info.
 *
 * Once this function is called, it assumes ownership of the
 * \p capture_info object and steals the reference of the
 * object. Regardless of success or failure, the calling function
 * should assumed that this function will own the object.
 *
 * \retval 0 on success
 * \retval -1 on error
 */
int hepv3_send_packet(struct hepv3_capture_info *capture_info);

/*!
 * \brief Get the preferred UUID type
 *
 * \since 13.10.0
 *
 * \return The type of UUID the packet should use
 */
enum hep_uuid_type hepv3_get_uuid_type(void);

/*!
 * \brief Return whether or not we're currently loaded and active
 *
 * \retval 0 The module is not loaded
 * \retval 1 The module is loaded
 */
int hepv3_is_loaded(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_RES_HEPV3_H */
