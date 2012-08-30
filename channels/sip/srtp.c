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

/*! \file srtp.c
 *
 * \brief SIP Secure RTP (SRTP)
 *
 * Specified in RFC 3711
 *
 * \author Mikael Magnusson <mikma@users.sourceforge.net>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/utils.h"
#include "include/srtp.h"

struct sip_srtp *sip_srtp_alloc(void)
{
	struct sip_srtp *srtp;

	srtp = ast_calloc(1, sizeof(*srtp));

	return srtp;
}

void sip_srtp_destroy(struct sip_srtp *srtp)
{
	if (srtp->crypto) {
		sdp_crypto_destroy(srtp->crypto);
	}
	srtp->crypto = NULL;
	ast_free(srtp);
}
