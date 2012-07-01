/* $Id$ */
/* 
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
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
#ifndef __PJMEDIA_H263_PACKETIZER_H__
#define __PJMEDIA_H263_PACKETIZER_H__


/**
 * @file h263_packetizer.h
 * @brief Packetizes/unpacketizes H.263 bitstream into RTP payload.
 */

#include <pj/pool.h>
#include <pj/types.h>

PJ_BEGIN_DECL


/**
 * Opaque declaration for H.263 packetizer.
 */
typedef struct pjmedia_h263_packetizer pjmedia_h263_packetizer;


/**
 * Enumeration of H.263 packetization modes.
 */
typedef enum
{
    /**
     * H.263 RTP packetization using RFC 4629.
     */
    PJMEDIA_H263_PACKETIZER_MODE_RFC4629,

    /**
     * H.263 RTP packetization using legacy RFC 2190.
     * This is currently not supported.
     */
    PJMEDIA_H263_PACKETIZER_MODE_RFC2190,

} pjmedia_h263_packetizer_mode;


/**
 * H.263 packetizer configuration.
 */
typedef struct pjmedia_h263_packetizer_cfg
{
    /**
     * Maximum payload length.
     * Default: PJMEDIA_MAX_MTU
     */
    int	mtu;

    /**
     * Packetization mode.
     * Default: PJMEDIA_H263_PACKETIZER_MODE_RFC4629
     */
    pjmedia_h263_packetizer_mode mode;

} pjmedia_h263_packetizer_cfg;


/**
 * Create H.263 packetizer.
 *
 * @param pool		The memory pool.
 * @param cfg		Packetizer settings, if NULL, default setting
 *			will be used.
 * @param p_pktz	Pointer to receive the packetizer.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_h263_packetizer_create(
				    pj_pool_t *pool,
				    const pjmedia_h263_packetizer_cfg *cfg,
				    pjmedia_h263_packetizer **p_pktz);


/**
 * Generate an RTP payload from a H.263 picture bitstream. Note that this
 * function will apply in-place processing, so the bitstream may be modified
 * during the packetization.
 *
 * @param pktz		The packetizer.
 * @param bits		The picture bitstream to be packetized.
 * @param bits_len	The length of the bitstream.
 * @param bits_pos	The bitstream offset to be packetized.
 * @param payload	The output payload.
 * @param payload_len	The output payload length.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_h263_packetize(pjmedia_h263_packetizer *pktz,
					    pj_uint8_t *bits,
                                            pj_size_t bits_len,
                                            unsigned *bits_pos,
                                            const pj_uint8_t **payload,
                                            pj_size_t *payload_len);


/**
 * Append an RTP payload to an H.263 picture bitstream. Note that in case of
 * noticing packet lost, application should keep calling this function with
 * payload pointer set to NULL, as the packetizer need to update its internal
 * state.
 *
 * @param pktz		The packetizer.
 * @param payload	The payload to be unpacketized.
 * @param payload_len	The payload length.
 * @param bits		The bitstream buffer.
 * @param bits_size	The bitstream buffer size.
 * @param bits_pos	The bitstream offset to put the unpacketized payload
 *			in the bitstream, upon return, this will be updated
 *			to the latest offset as a result of the unpacketized
 *			payload.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_h263_unpacketize(pjmedia_h263_packetizer *pktz,
					      const pj_uint8_t *payload,
                                              pj_size_t payload_len,
                                              pj_uint8_t *bits,
                                              pj_size_t bits_size,
					      unsigned *bits_pos);


PJ_END_DECL


#endif	/* __PJMEDIA_H263_PACKETIZER_H__ */
