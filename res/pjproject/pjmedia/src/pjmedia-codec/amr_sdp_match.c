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
#include <pjmedia-codec/amr_sdp_match.h>
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


/* Toggle AMR octet-align setting in the fmtp. */
static pj_status_t amr_toggle_octet_align(pj_pool_t *pool,
					  pjmedia_sdp_media *media,
					  unsigned fmt_idx)
{
    pjmedia_sdp_attr *attr;
    pjmedia_sdp_fmtp fmtp;
    const pj_str_t STR_OCTET_ALIGN = {"octet-align=", 12};
    
    enum { MAX_FMTP_STR_LEN = 160 };

    attr = pjmedia_sdp_media_find_attr2(media, "fmtp", 
					&media->desc.fmt[fmt_idx]);
    /* Check if the AMR media format has FMTP attribute */
    if (attr) {
	char *p;
	pj_status_t status;

	status = pjmedia_sdp_attr_get_fmtp(attr, &fmtp);
	if (status != PJ_SUCCESS)
	    return status;

	/* Check if the fmtp has octet-align field. */
	p = pj_stristr(&fmtp.fmt_param, &STR_OCTET_ALIGN);
	if (p) {
	    /* It has, just toggle the value */
	    unsigned octet_align;
	    pj_str_t s;

	    pj_strset(&s, p + STR_OCTET_ALIGN.slen, fmtp.fmt_param.slen -
		      (p - fmtp.fmt_param.ptr) - STR_OCTET_ALIGN.slen);
	    octet_align = pj_strtoul(&s);
	    *(p + STR_OCTET_ALIGN.slen) = (char)(octet_align? '0' : '1');
	} else {
	    /* It doesn't, append octet-align field */
	    char buf[MAX_FMTP_STR_LEN];

	    pj_ansi_snprintf(buf, MAX_FMTP_STR_LEN, "%.*s;octet-align=1",
			     (int)fmtp.fmt_param.slen, fmtp.fmt_param.ptr);
	    attr->value = pj_strdup3(pool, buf);
	}
    } else {
	/* Add new attribute for the AMR media format with octet-align 
	 * field set.
	 */
	char buf[MAX_FMTP_STR_LEN];

	attr = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_attr);
	attr->name = pj_str("fmtp");
	pj_ansi_snprintf(buf, MAX_FMTP_STR_LEN, "%.*s octet-align=1",
			 (int)media->desc.fmt[fmt_idx].slen,
		         media->desc.fmt[fmt_idx].ptr);
	attr->value = pj_strdup3(pool, buf);
	media->attr[media->attr_count++] = attr;
    }

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_codec_amr_match_sdp( pj_pool_t *pool,
						 pjmedia_sdp_media *offer,
						 unsigned o_fmt_idx,
						 pjmedia_sdp_media *answer,
						 unsigned a_fmt_idx,
						 unsigned option)
{
    /* Negotiated format-param field-names constants. */
    const pj_str_t STR_OCTET_ALIGN	= {"octet-align=", 12};
    const pj_str_t STR_CRC		= {"crc=", 4};
    const pj_str_t STR_ROBUST_SORTING	= {"robust-sorting=", 15};
    const pj_str_t STR_INTERLEAVING	= {"interleaving=", 13};

    /* Negotiated params and their default values. */
    unsigned a_octet_align = 0, o_octet_align = 0;
    unsigned a_crc = 0, o_crc = 0;
    unsigned a_robust_sorting = 0, o_robust_sorting = 0;
    unsigned a_interleaving = 0, o_interleaving = 0;

    const pjmedia_sdp_attr *attr_ans;
    const pjmedia_sdp_attr *attr_ofr;
    pjmedia_sdp_fmtp fmtp;
    pj_status_t status;

    /* Parse offerer FMTP */
    attr_ofr = pjmedia_sdp_media_find_attr2(offer, "fmtp", 
					    &offer->desc.fmt[o_fmt_idx]);
    if (attr_ofr) {
	status = pjmedia_sdp_attr_get_fmtp(attr_ofr, &fmtp);
	if (status != PJ_SUCCESS)
	    return status;

	GET_FMTP_IVAL(o_octet_align, fmtp, STR_OCTET_ALIGN, 0);
	GET_FMTP_IVAL(o_crc, fmtp, STR_CRC, 0);
	GET_FMTP_IVAL(o_robust_sorting, fmtp, STR_ROBUST_SORTING, 0);
	GET_FMTP_IVAL(o_interleaving, fmtp, STR_INTERLEAVING, 0);
    }

    /* Parse answerer FMTP */
    attr_ans = pjmedia_sdp_media_find_attr2(answer, "fmtp", 
					    &answer->desc.fmt[a_fmt_idx]);
    if (attr_ans) {
	status = pjmedia_sdp_attr_get_fmtp(attr_ans, &fmtp);
	if (status != PJ_SUCCESS)
	    return status;

	GET_FMTP_IVAL(a_octet_align, fmtp, STR_OCTET_ALIGN, 0);
	GET_FMTP_IVAL(a_crc, fmtp, STR_CRC, 0);
	GET_FMTP_IVAL(a_robust_sorting, fmtp, STR_ROBUST_SORTING, 0);
	GET_FMTP_IVAL(a_interleaving, fmtp, STR_INTERLEAVING, 0);
    }

    /* First, match crc, robust-sorting, interleaving settings */
    if (a_crc != o_crc ||
	a_robust_sorting != o_robust_sorting ||
	a_interleaving != o_interleaving)
    {
	return PJMEDIA_SDP_EFORMATNOTEQUAL;
    }

    /* Match octet-align setting */
    if (a_octet_align != o_octet_align) {
	/* Check if answer can be modified to match to the offer */
	if (option & PJMEDIA_SDP_NEG_FMT_MATCH_ALLOW_MODIFY_ANSWER) {
	    status = amr_toggle_octet_align(pool, answer, a_fmt_idx);
	    return status;
	} else {
	    return PJMEDIA_SDP_EFORMATNOTEQUAL;
	}
    }

    return PJ_SUCCESS;
}
