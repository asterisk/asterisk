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
#ifndef __PJMEDIA_VID_CODEC_UTIL_H__
#define __PJMEDIA_VID_CODEC_UTIL_H__


/**
 * @file vid_codec_util.h
 * @brief Video codec utilities.
 */

#include <pjmedia/vid_codec.h>
#include <pjmedia/sdp_neg.h>

PJ_BEGIN_DECL


/**
 * Definition of H.263 parameters.
 */
typedef struct pjmedia_vid_codec_h263_fmtp
{
    unsigned mpi_cnt;		    /**< # of parsed MPI param		    */
    struct mpi {
	pjmedia_rect_size   size;   /**< Picture size/resolution	    */
	unsigned	    val;    /**< MPI value			    */
    } mpi[32];			    /**< Minimum Picture Interval parameter */

} pjmedia_vid_codec_h263_fmtp;


/**
 * Parse SDP fmtp of H.263.
 *
 * @param fmtp		The H.263 SDP fmtp to be parsed.
 * @param h263_fmtp	The parsing result.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_codec_h263_parse_fmtp(
				const pjmedia_codec_fmtp *fmtp,
				pjmedia_vid_codec_h263_fmtp *h263_fmtp);


/**
 * Parse, negotiate, and apply the encoding and decoding SDP fmtp of H.263
 * in the specified codec parameter.
 *
 * @param param		The codec parameter.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_codec_h263_apply_fmtp(
				pjmedia_vid_codec_param *param);


/**
 * Definition of H.264 parameters.
 */
typedef struct pjmedia_vid_codec_h264_fmtp
{
    /* profile-level-id */
    pj_uint8_t	    profile_idc;    /**< Profile ID			    */
    pj_uint8_t	    profile_iop;    /**< Profile constraints bits	    */
    pj_uint8_t	    level;	    /**< Level				    */

    /* packetization-mode */
    pj_uint8_t	    packetization_mode;	/**< Packetization mode		    */

    /* max-mbps, max-fs, max-cpb, max-dpb, and max-br */
    unsigned	    max_mbps;	    /**< Max macroblock processing rate	    */
    unsigned	    max_fs;	    /**< Max frame size (in macroblocks)    */
    unsigned	    max_cpb;	    /**< Max coded picture buffer size	    */
    unsigned	    max_dpb;	    /**< Max decoded picture buffer size    */
    unsigned	    max_br;	    /**< Max video bit rate		    */

    /* sprop-parameter-sets, in NAL units */
    pj_size_t	    sprop_param_sets_len;   /**< Parameter set length	    */
    pj_uint8_t	    sprop_param_sets[256];  /**< Parameter set (SPS & PPS),
						 in NAL unit bitstream	    */

} pjmedia_vid_codec_h264_fmtp;


/**
 * Parse SDP fmtp of H.264.
 *
 * @param fmtp		The H.264 SDP fmtp to be parsed.
 * @param h264_fmtp	The parsing result.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_codec_h264_parse_fmtp(
				const pjmedia_codec_fmtp *fmtp,
				pjmedia_vid_codec_h264_fmtp *h264_fmtp);


/**
 * Match H.264 format in the SDP media offer and answer. This will compare
 * H.264 identifier parameters in SDP fmtp, i.e: "profile-level-id" and
 * "packetization-mode" fields. For better interoperability, when the option
 * #PJMEDIA_SDP_NEG_FMT_MATCH_ALLOW_MODIFY_ANSWER is set, this function
 * may update the answer so the parameters in the answer match to ones
 * in the offer.
 *
 * @param pool		The memory pool.
 * @param offer		The SDP media offer.
 * @param o_fmt_idx	Index of the H.264 format in the SDP media offer.
 * @param answer	The SDP media answer.
 * @param a_fmt_idx	Index of the H.264 format in the SDP media answer.
 * @param option	The format matching option, see
 *			#pjmedia_sdp_neg_fmt_match_flag.
 *
 * @return		PJ_SUCCESS when the formats in offer and answer match.
 */
PJ_DECL(pj_status_t) pjmedia_vid_codec_h264_match_sdp(
						pj_pool_t *pool,
						pjmedia_sdp_media *offer,
						unsigned o_fmt_idx,
						pjmedia_sdp_media *answer,
						unsigned a_fmt_idx,
						unsigned option);


/**
 * Parse and apply the encoding and decoding SDP fmtp of H.264 in the
 * specified codec parameter. This will validate size and fps to conform
 * to H.264 level specified in SDP fmtp "profile-level-id".
 *
 * @param param		The codec parameter.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_codec_h264_apply_fmtp(
				pjmedia_vid_codec_param *param);


PJ_END_DECL


#endif	/* __PJMEDIA_VID_CODEC_UTIL_H__ */
