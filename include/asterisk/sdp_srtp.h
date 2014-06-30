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

/*! \file sdp_srtp.h
 *
 * \brief SRTP and SDP Security descriptions
 *
 * Specified in RFC 4568
 * Specified in RFC 3711
 *
 * \author Mikael Magnusson <mikma@users.sourceforge.net>
 */

#ifndef _SDP_SRTP_H
#define _SDP_SRTP_H

#include <asterisk/rtp_engine.h>

struct ast_sdp_crypto;

/*! \brief structure for secure RTP audio */
struct ast_sdp_srtp {
	unsigned int flags;
	struct ast_sdp_crypto *crypto;
};

/* SRTP flags */
#define AST_SRTP_CRYPTO_OFFER_OK	(1 << 1)
#define AST_SRTP_CRYPTO_TAG_32		(1 << 2)
#define AST_SRTP_CRYPTO_TAG_80		(1 << 3)

/*!
 * \brief allocate a ast_sdp_srtp structure
 * \retval a new malloc'd ast_sdp_srtp structure on success
 * \retval NULL on failure
*/
struct ast_sdp_srtp *ast_sdp_srtp_alloc(void);

/*!
 * \brief free a ast_sdp_srtp structure
 * \param srtp a ast_sdp_srtp structure
*/
void ast_sdp_srtp_destroy(struct ast_sdp_srtp *srtp);

/*! \brief Initialize an return an ast_sdp_crypto struct
 *
 * \details
 * This function allocates a new ast_sdp_crypto struct and initializes its values
 *
 * \retval NULL on failure
 * \retval a pointer to a  new ast_sdp_crypto structure
 */
struct ast_sdp_crypto *ast_sdp_crypto_alloc(void);

/*! \brief Destroy a previously allocated ast_sdp_crypto struct */
void ast_sdp_crypto_destroy(struct ast_sdp_crypto *crypto);

/*! \brief Parse the a=crypto line from SDP and set appropriate values on the
 * ast_sdp_crypto struct.
 *
 * The attribute line should already have "a=crypto:" removed.
 *
 * \param p A valid ast_sdp_crypto struct
 * \param attr the a:crypto line from SDP
 * \param rtp The rtp instance associated with the SDP being parsed
 * \param srtp SRTP structure
 *
 * \retval 0 success
 * \retval nonzero failure
 */
int ast_sdp_crypto_process(struct ast_rtp_instance *rtp, struct ast_sdp_srtp *srtp, const char *attr);

/*! \brief Generate an SRTP a=crypto offer
 *
 * \details
 * The offer is stored on the ast_sdp_crypto struct in a_crypto
 *
 * \param p A valid ast_sdp_crypto struct
 * \param taglen Length
 *
 * \retval 0 success
 * \retval nonzero failure
 */
int ast_sdp_crypto_build_offer(struct ast_sdp_crypto *p, int taglen);


/*! \brief Get the crypto attribute line for the srtp structure
 *
 * The attribute line does not contain the initial "a=crypto:" and does
 * not terminate with "\r\n".
 *
 * \param srtp The ast_sdp_srtp structure for which to get an attribute line
 * \param dtls_enabled Whether this connection is encrypted with datagram TLS
 * \param default_taglen_32 Whether to default to a tag length of 32 instead of 80
 *
 * \retval An attribute line containing cryptographic information
 * \retval NULL if the srtp structure does not require an attribute line containing crypto information
 */
const char *ast_sdp_srtp_get_attrib(struct ast_sdp_srtp *srtp, int dtls_enabled, int default_taglen_32);

/*! \brief Get the RTP profile in use by a media session
 *
 * \param sdes_active Whether the media session is using SDES-SRTP
 * \param instance The RTP instance associated with this media session
 * \param using_avpf Whether the media session is using early feedback (AVPF)
 * \param force_avp Force SAVP or SAVPF profile when DTLS is in use
 *
 * \retval A non-allocated string describing the profile in use (does not need to be freed)
 */
char *ast_sdp_get_rtp_profile(unsigned int sdes_active, struct ast_rtp_instance *instance, unsigned int using_avpf,
	unsigned int force_avp);
#endif	/* _SDP_CRYPTO_H */
