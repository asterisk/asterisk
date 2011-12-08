/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006 - 2007, Mikael Magnusson
 *
 * Mikael Magnusson <mikma@users.sourceforge.net>
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

/*! \file sip_srtp.h
 *
 * \brief SIP Secure RTP (SRTP)
 *
 * Specified in RFC 3711
 *
 * \author Mikael Magnusson <mikma@users.sourceforge.net>
 */

#ifndef _SIP_SRTP_H
#define _SIP_SRTP_H

#include "sdp_crypto.h"

/* SRTP flags */
#define SRTP_ENCR_OPTIONAL	(1 << 1)	/* SRTP encryption optional */
#define SRTP_CRYPTO_ENABLE	(1 << 2)
#define SRTP_CRYPTO_OFFER_OK	(1 << 3)
#define SRTP_CRYPTO_TAG_32	(1 << 4)
#define SRTP_CRYPTO_TAG_80	(1 << 5)

/*! \brief structure for secure RTP audio */
struct sip_srtp {
	unsigned int flags;
	struct sdp_crypto *crypto;
};

/*!
 * \brief allocate a sip_srtp structure
 * \retval a new malloc'd sip_srtp structure on success
 * \retval NULL on failure
*/
struct sip_srtp *sip_srtp_alloc(void);

/*!
 * \brief free a sip_srtp structure
 * \param srtp a sip_srtp structure
*/
void sip_srtp_destroy(struct sip_srtp *srtp);

#endif	/* _SIP_SRTP_H */
