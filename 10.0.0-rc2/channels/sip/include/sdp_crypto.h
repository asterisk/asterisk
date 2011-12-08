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

/*! \file sdp_crypto.h
 *
 * \brief SDP Security descriptions
 *
 * Specified in RFC 4568
 *
 * \author Mikael Magnusson <mikma@users.sourceforge.net>
 */

#ifndef _SDP_CRYPTO_H
#define _SDP_CRYPTO_H

#include <asterisk/rtp_engine.h>

struct sdp_crypto;
struct sip_srtp;

/*! \brief Initialize an return an sdp_crypto struct
 *
 * \details
 * This function allocates a new sdp_crypto struct and initializes its values
 *
 * \retval NULL on failure
 * \retval a pointer to a  new sdp_crypto structure
 */
struct sdp_crypto *sdp_crypto_setup(void);

/*! \brief Destroy a previously allocated sdp_crypto struct */
void sdp_crypto_destroy(struct sdp_crypto *crypto);

/*! \brief Parse the a=crypto line from SDP and set appropriate values on the
 * sdp_crypto struct.
 *
 * \param p A valid sdp_crypto struct
 * \param attr the a:crypto line from SDP
 * \param rtp The rtp instance associated with the SDP being parsed
 * \param srtp SRTP structure
 *
 * \retval 0 success
 * \retval nonzero failure
 */
int sdp_crypto_process(struct sdp_crypto *p, const char *attr, struct ast_rtp_instance *rtp, struct sip_srtp *srtp);


/*! \brief Generate an SRTP a=crypto offer
 *
 * \details
 * The offer is stored on the sdp_crypto struct in a_crypto
 *
 * \param A valid sdp_crypto struct
 *
 * \retval 0 success
 * \retval nonzero failure
 */
int sdp_crypto_offer(struct sdp_crypto *p, int taglen);


/*! \brief Return the a_crypto value of the sdp_crypto struct
 *
 * \param p An sdp_crypto struct that has had sdp_crypto_offer called
 *
 * \retval The value of the a_crypto for p
 */
const char *sdp_crypto_attrib(struct sdp_crypto *p);

#endif	/* _SDP_CRYPTO_H */
