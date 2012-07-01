/* $Id$ */
/*
 * Copyright (C) 2011 Teluu Inc. (http://www.teluu.com)
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
#include <pjmedia/vid_stream.h>
#include <pjmedia/sdp_neg.h>
#include <pjmedia/stream_common.h>
#include <pj/ctype.h>
#include <pj/rand.h>


static const pj_str_t ID_VIDEO = { "video", 5};
static const pj_str_t ID_IN = { "IN", 2 };
static const pj_str_t ID_IP4 = { "IP4", 3};
static const pj_str_t ID_IP6 = { "IP6", 3};
static const pj_str_t ID_RTP_AVP = { "RTP/AVP", 7 };
static const pj_str_t ID_RTP_SAVP = { "RTP/SAVP", 8 };
//static const pj_str_t ID_SDP_NAME = { "pjmedia", 7 };
static const pj_str_t ID_RTPMAP = { "rtpmap", 6 };

static const pj_str_t STR_INACTIVE = { "inactive", 8 };
static const pj_str_t STR_SENDRECV = { "sendrecv", 8 };
static const pj_str_t STR_SENDONLY = { "sendonly", 8 };
static const pj_str_t STR_RECVONLY = { "recvonly", 8 };


/*
 * Internal function for collecting codec info and param from the SDP media.
 */
static pj_status_t get_video_codec_info_param(pjmedia_vid_stream_info *si,
					      pj_pool_t *pool,
					      pjmedia_vid_codec_mgr *mgr,
					      const pjmedia_sdp_media *local_m,
					      const pjmedia_sdp_media *rem_m)
{
    unsigned pt = 0;
    const pjmedia_vid_codec_info *p_info;
    pj_status_t status;

    pt = pj_strtoul(&local_m->desc.fmt[0]);

    /* Get payload type for receiving direction */
    si->rx_pt = pt;

    /* Get codec info and payload type for transmitting direction. */
    if (pt < 96) {
	/* For static payload types, get the codec info from codec manager. */
	status = pjmedia_vid_codec_mgr_get_codec_info(mgr, pt, &p_info);
	if (status != PJ_SUCCESS)
	    return status;

	si->codec_info = *p_info;

	/* Get payload type for transmitting direction.
	 * For static payload type, pt's are symetric.
	 */
	si->tx_pt = pt;
    } else {
	const pjmedia_sdp_attr *attr;
	pjmedia_sdp_rtpmap *rtpmap;
	pjmedia_codec_id codec_id;
	pj_str_t codec_id_st;
	unsigned i;

	/* Determine payload type for outgoing channel, by finding
	 * dynamic payload type in remote SDP that matches the answer.
	 */
	si->tx_pt = 0xFFFF;
	for (i=0; i<rem_m->desc.fmt_count; ++i) {
	    if (pjmedia_sdp_neg_fmt_match(NULL,
					  (pjmedia_sdp_media*)local_m, 0,
					  (pjmedia_sdp_media*)rem_m, i, 0) ==
		PJ_SUCCESS)
	    {
		/* Found matched codec. */
		si->tx_pt = pj_strtoul(&rem_m->desc.fmt[i]);
		break;
	    }
	}

	if (si->tx_pt == 0xFFFF)
	    return PJMEDIA_EMISSINGRTPMAP;

	/* For dynamic payload types, get codec name from the rtpmap */
	attr = pjmedia_sdp_media_find_attr(local_m, &ID_RTPMAP,
					   &local_m->desc.fmt[0]);
	if (attr == NULL)
	    return PJMEDIA_EMISSINGRTPMAP;

	status = pjmedia_sdp_attr_to_rtpmap(pool, attr, &rtpmap);
	if (status != PJ_SUCCESS)
	    return status;

	/* Then get the codec info from the codec manager */
	pj_ansi_snprintf(codec_id, sizeof(codec_id), "%.*s/",
			 (int)rtpmap->enc_name.slen, rtpmap->enc_name.ptr);
	codec_id_st = pj_str(codec_id);
	i = 1;
	status = pjmedia_vid_codec_mgr_find_codecs_by_id(mgr, &codec_id_st,
							 &i, &p_info, NULL);
	if (status != PJ_SUCCESS)
	    return status;

	si->codec_info = *p_info;
    }


    /* Request for codec with the correct packing for streaming */
    si->codec_info.packings = PJMEDIA_VID_PACKING_PACKETS;

    /* Now that we have codec info, get the codec param. */
    si->codec_param = PJ_POOL_ALLOC_T(pool, pjmedia_vid_codec_param);
    status = pjmedia_vid_codec_mgr_get_default_param(mgr,
						     &si->codec_info,
						     si->codec_param);

    /* Adjust encoding bitrate, if higher than remote preference. The remote
     * bitrate preference is read from SDP "b=TIAS" line in media level.
     */
    if ((si->dir & PJMEDIA_DIR_ENCODING) && rem_m->bandw_count) {
	unsigned i, bandw = 0;

	for (i = 0; i < rem_m->bandw_count; ++i) {
	    const pj_str_t STR_BANDW_MODIFIER_TIAS = { "TIAS", 4 };
	    if (!pj_stricmp(&rem_m->bandw[i]->modifier,
		&STR_BANDW_MODIFIER_TIAS))
	    {
		bandw = rem_m->bandw[i]->value;
		break;
	    }
	}

	if (bandw) {
	    pjmedia_video_format_detail *enc_vfd;
	    enc_vfd = pjmedia_format_get_video_format_detail(
					&si->codec_param->enc_fmt, PJ_TRUE);
	    if (!enc_vfd->avg_bps || enc_vfd->avg_bps > bandw)
		enc_vfd->avg_bps = bandw * 3 / 4;
	    if (!enc_vfd->max_bps || enc_vfd->max_bps > bandw)
		enc_vfd->max_bps = bandw;
	}
    }

    /* Get remote fmtp for our encoder. */
    pjmedia_stream_info_parse_fmtp(pool, rem_m, si->tx_pt,
				   &si->codec_param->enc_fmtp);

    /* Get local fmtp for our decoder. */
    pjmedia_stream_info_parse_fmtp(pool, local_m, si->rx_pt,
				   &si->codec_param->dec_fmtp);

    /* When direction is NONE (it means SDP negotiation has failed) we don't
     * need to return a failure here, as returning failure will cause
     * the whole SDP to be rejected. See ticket #:
     *	http://
     *
     * Thanks Alain Totouom
     */
    if (status != PJ_SUCCESS && si->dir != PJMEDIA_DIR_NONE)
	return status;

    return PJ_SUCCESS;
}



/*
 * Create stream info from SDP media line.
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_info_from_sdp(
					   pjmedia_vid_stream_info *si,
					   pj_pool_t *pool,
					   pjmedia_endpt *endpt,
					   const pjmedia_sdp_session *local,
					   const pjmedia_sdp_session *remote,
					   unsigned stream_idx)
{
    const pjmedia_sdp_attr *attr;
    const pjmedia_sdp_media *local_m;
    const pjmedia_sdp_media *rem_m;
    const pjmedia_sdp_conn *local_conn;
    const pjmedia_sdp_conn *rem_conn;
    int rem_af, local_af;
    pj_sockaddr local_addr;
    pj_status_t status;

    PJ_UNUSED_ARG(endpt);

    /* Validate arguments: */
    PJ_ASSERT_RETURN(pool && si && local && remote, PJ_EINVAL);
    PJ_ASSERT_RETURN(stream_idx < local->media_count, PJ_EINVAL);
    PJ_ASSERT_RETURN(stream_idx < remote->media_count, PJ_EINVAL);

    /* Keep SDP shortcuts */
    local_m = local->media[stream_idx];
    rem_m = remote->media[stream_idx];

    local_conn = local_m->conn ? local_m->conn : local->conn;
    if (local_conn == NULL)
	return PJMEDIA_SDP_EMISSINGCONN;

    rem_conn = rem_m->conn ? rem_m->conn : remote->conn;
    if (rem_conn == NULL)
	return PJMEDIA_SDP_EMISSINGCONN;

    /* Media type must be video */
    if (pj_stricmp(&local_m->desc.media, &ID_VIDEO) != 0)
	return PJMEDIA_EINVALIMEDIATYPE;


    /* Reset: */

    pj_bzero(si, sizeof(*si));

    /* Media type: */
    si->type = PJMEDIA_TYPE_VIDEO;

    /* Transport protocol */

    /* At this point, transport type must be compatible,
     * the transport instance will do more validation later.
     */
    status = pjmedia_sdp_transport_cmp(&rem_m->desc.transport,
				       &local_m->desc.transport);
    if (status != PJ_SUCCESS)
	return PJMEDIA_SDPNEG_EINVANSTP;

    if (pj_stricmp(&local_m->desc.transport, &ID_RTP_AVP) == 0) {

	si->proto = PJMEDIA_TP_PROTO_RTP_AVP;

    } else if (pj_stricmp(&local_m->desc.transport, &ID_RTP_SAVP) == 0) {

	si->proto = PJMEDIA_TP_PROTO_RTP_SAVP;

    } else {

	si->proto = PJMEDIA_TP_PROTO_UNKNOWN;
	return PJ_SUCCESS;
    }


    /* Check address family in remote SDP */
    rem_af = pj_AF_UNSPEC();
    if (pj_stricmp(&rem_conn->net_type, &ID_IN)==0) {
	if (pj_stricmp(&rem_conn->addr_type, &ID_IP4)==0) {
	    rem_af = pj_AF_INET();
	} else if (pj_stricmp(&rem_conn->addr_type, &ID_IP6)==0) {
	    rem_af = pj_AF_INET6();
	}
    }

    if (rem_af==pj_AF_UNSPEC()) {
	/* Unsupported address family */
	return PJ_EAFNOTSUP;
    }

    /* Set remote address: */
    status = pj_sockaddr_init(rem_af, &si->rem_addr, &rem_conn->addr,
			      rem_m->desc.port);
    if (status != PJ_SUCCESS) {
	/* Invalid IP address. */
	return PJMEDIA_EINVALIDIP;
    }

    /* Check address family of local info */
    local_af = pj_AF_UNSPEC();
    if (pj_stricmp(&local_conn->net_type, &ID_IN)==0) {
	if (pj_stricmp(&local_conn->addr_type, &ID_IP4)==0) {
	    local_af = pj_AF_INET();
	} else if (pj_stricmp(&local_conn->addr_type, &ID_IP6)==0) {
	    local_af = pj_AF_INET6();
	}
    }

    if (local_af==pj_AF_UNSPEC()) {
	/* Unsupported address family */
	return PJ_SUCCESS;
    }

    /* Set remote address: */
    status = pj_sockaddr_init(local_af, &local_addr, &local_conn->addr,
			      local_m->desc.port);
    if (status != PJ_SUCCESS) {
	/* Invalid IP address. */
	return PJMEDIA_EINVALIDIP;
    }

    /* Local and remote address family must match */
    if (local_af != rem_af)
	return PJ_EAFNOTSUP;

    /* Media direction: */

    if (local_m->desc.port == 0 ||
	pj_sockaddr_has_addr(&local_addr)==PJ_FALSE ||
	pj_sockaddr_has_addr(&si->rem_addr)==PJ_FALSE ||
	pjmedia_sdp_media_find_attr(local_m, &STR_INACTIVE, NULL)!=NULL)
    {
	/* Inactive stream. */

	si->dir = PJMEDIA_DIR_NONE;

    } else if (pjmedia_sdp_media_find_attr(local_m, &STR_SENDONLY, NULL)!=NULL) {

	/* Send only stream. */

	si->dir = PJMEDIA_DIR_ENCODING;

    } else if (pjmedia_sdp_media_find_attr(local_m, &STR_RECVONLY, NULL)!=NULL) {

	/* Recv only stream. */

	si->dir = PJMEDIA_DIR_DECODING;

    } else {

	/* Send and receive stream. */

	si->dir = PJMEDIA_DIR_ENCODING_DECODING;

    }

    /* No need to do anything else if stream is rejected */
    if (local_m->desc.port == 0) {
	return PJ_SUCCESS;
    }

    /* If "rtcp" attribute is present in the SDP, set the RTCP address
     * from that attribute. Otherwise, calculate from RTP address.
     */
    attr = pjmedia_sdp_attr_find2(rem_m->attr_count, rem_m->attr,
				  "rtcp", NULL);
    if (attr) {
	pjmedia_sdp_rtcp_attr rtcp;
	status = pjmedia_sdp_attr_get_rtcp(attr, &rtcp);
	if (status == PJ_SUCCESS) {
	    if (rtcp.addr.slen) {
		status = pj_sockaddr_init(rem_af, &si->rem_rtcp, &rtcp.addr,
					  (pj_uint16_t)rtcp.port);
	    } else {
		pj_sockaddr_init(rem_af, &si->rem_rtcp, NULL,
				 (pj_uint16_t)rtcp.port);
		pj_memcpy(pj_sockaddr_get_addr(&si->rem_rtcp),
		          pj_sockaddr_get_addr(&si->rem_addr),
			  pj_sockaddr_get_addr_len(&si->rem_addr));
	    }
	}
    }

    if (!pj_sockaddr_has_addr(&si->rem_rtcp)) {
	int rtcp_port;

	pj_memcpy(&si->rem_rtcp, &si->rem_addr, sizeof(pj_sockaddr));
	rtcp_port = pj_sockaddr_get_port(&si->rem_addr) + 1;
	pj_sockaddr_set_port(&si->rem_rtcp, (pj_uint16_t)rtcp_port);
    }

    /* Get codec info and param */
    status = get_video_codec_info_param(si, pool, NULL, local_m, rem_m);

    /* Leave SSRC to random. */
    si->ssrc = pj_rand();

    /* Set default jitter buffer parameter. */
    si->jb_init = si->jb_max = si->jb_min_pre = si->jb_max_pre = -1;

    return status;
}


