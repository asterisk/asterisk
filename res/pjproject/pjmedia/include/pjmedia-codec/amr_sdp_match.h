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
#ifndef __PJMEDIA_CODEC_AMR_SDP_MATCH_H__
#define __PJMEDIA_CODEC_AMR_SDP_MATCH_H__


/**
 * @file g7221_sdp_match.h
 * @brief Special SDP format match for AMR-NB and AMR-WB.
 */

#include <pjmedia/sdp_neg.h>

PJ_BEGIN_DECL


/* Match AMR-NB and AMR-WB format in the SDP media offer and answer. This
 * function will match some AMR settings in the SDP format parameters, i.e:
 * octet-align, crc, robust-sorting, interleaving. Note that, for answerer,
 * if octet-align mode needs to be adaptable to offerer setting, application
 * should set #PJMEDIA_SDP_NEG_FMT_MATCH_ALLOW_MODIFY_ANSWER in the option.
 *
 * @param pool		The memory pool.
 * @param offer		The SDP media offer.
 * @param o_fmt_idx	Index of the AMR format in the SDP media offer.
 * @param answer	The SDP media answer.
 * @param a_fmt_idx	Index of the AMR format in the SDP media answer.
 * @param option	The format matching option, see
 *			#pjmedia_sdp_neg_fmt_match_flag.
 *
 * @return		PJ_SUCCESS when the formats in offer and answer match.
 */
PJ_DECL(pj_status_t) pjmedia_codec_amr_match_sdp( pj_pool_t *pool,
						  pjmedia_sdp_media *offer,
						  unsigned o_fmt_idx,
						  pjmedia_sdp_media *answer,
						  unsigned a_fmt_idx,
						  unsigned option);


PJ_END_DECL


#endif	/* __PJMEDIA_CODEC_AMR_SDP_MATCH_H__ */
