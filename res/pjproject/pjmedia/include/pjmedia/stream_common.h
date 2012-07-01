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
#ifndef __PJMEDIA_STREAM_COMMON_H__
#define __PJMEDIA_STREAM_COMMON_H__


/**
 * @file stream_common.h
 * @brief Stream common functions.
 */

#include <pjmedia/codec.h>
#include <pjmedia/sdp.h>


PJ_BEGIN_DECL

/**
 * This is internal function for parsing SDP format parameter of specific
 * format or payload type, used by stream in generating stream info from SDP.
 *
 * @param pool		Pool to allocate memory, if pool is NULL, the fmtp
 *			string pointers will point to the original string in
 *			the SDP media descriptor.
 * @param m		The SDP media containing the format parameter to
 *			be parsed.
 * @param pt		The format or payload type.
 * @param fmtp		The format parameter to store the parsing result.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_stream_info_parse_fmtp(pj_pool_t *pool,
						    const pjmedia_sdp_media *m,
						    unsigned pt,
						    pjmedia_codec_fmtp *fmtp);


PJ_END_DECL


#endif /* __PJMEDIA_STREAM_COMMON_H__ */
