/* $Id$ */
/* 
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#include <pjmedia-codec/g7221_sdp_match.h>
#include <pjmedia/errno.h>
#include <pj/pool.h>
#include <pj/string.h>


#define GET_FMTP_IVAL_BASE(ival, base, fmtp, param, default_val) \
    do { \
	pj_str_t s; \
	char *p; \
	p = pj_stristr(&fmtp.fmt_param, &param); \
	if (!p) { \
	    ival = default_val; \
	    break; \
	} \
	pj_strset(&s, p + param.slen, fmtp.fmt_param.slen - \
		  (p - fmtp.fmt_param.ptr) - param.slen); \
	ival = pj_strtoul2(&s, NULL, base); \
    } while (0)

#define GET_FMTP_IVAL(ival, fmtp, param, default_val) \
	GET_FMTP_IVAL_BASE(ival, 10, fmtp, param, default_val)



PJ_DEF(pj_status_t) pjmedia_codec_g7221_match_sdp(pj_pool_t *pool,
						  pjmedia_sdp_media *offer,
						  unsigned o_fmt_idx,
						  pjmedia_sdp_media *answer,
						  unsigned a_fmt_idx,
						  unsigned option)
{
    const pjmedia_sdp_attr *attr_ans;
    const pjmedia_sdp_attr *attr_ofr;
    pjmedia_sdp_fmtp fmtp;
    unsigned a_bitrate, o_bitrate;
    const pj_str_t bitrate = {"bitrate=", 8};
    pj_status_t status;

    PJ_UNUSED_ARG(pool);
    PJ_UNUSED_ARG(option);

    /* Parse offer */
    attr_ofr = pjmedia_sdp_media_find_attr2(offer, "fmtp", 
					    &offer->desc.fmt[o_fmt_idx]);
    if (!attr_ofr)
	return PJMEDIA_SDP_EINFMTP;

    status = pjmedia_sdp_attr_get_fmtp(attr_ofr, &fmtp);
    if (status != PJ_SUCCESS)
	return status;

    GET_FMTP_IVAL(o_bitrate, fmtp, bitrate, 0);

    /* Parse answer */
    attr_ans = pjmedia_sdp_media_find_attr2(answer, "fmtp", 
					    &answer->desc.fmt[a_fmt_idx]);
    if (!attr_ans)
	return PJMEDIA_SDP_EINFMTP;

    status = pjmedia_sdp_attr_get_fmtp(attr_ans, &fmtp);
    if (status != PJ_SUCCESS)
	return status;

    GET_FMTP_IVAL(a_bitrate, fmtp, bitrate, 0);

    /* Compare bitrate in answer and offer. */
    if (a_bitrate != o_bitrate)
	return PJMEDIA_SDP_EFORMATNOTEQUAL;

    return PJ_SUCCESS;
}
