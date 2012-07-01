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
#include <pjmedia-codec/h264_packetizer.h>
#include <pjmedia/types.h>
#include <pj/assert.h>
#include <pj/errno.h>
#include <pj/log.h>
#include <pj/pool.h>
#include <pj/string.h>


#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)


#define THIS_FILE		"h264_packetizer.c"

#define DBG_PACKETIZE		0
#define DBG_UNPACKETIZE		0


/* H.264 packetizer definition */
struct pjmedia_h264_packetizer
{
    /* Current settings */
    pjmedia_h264_packetizer_cfg cfg;
    
    /* Unpacketizer state */
    unsigned	    unpack_last_sync_pos;
    pj_bool_t	    unpack_prev_lost;
};


/* Enumeration of H.264 NAL unit types */
enum
{
    NAL_TYPE_SINGLE_NAL_MIN	= 1,
    NAL_TYPE_SINGLE_NAL_MAX	= 23,
    NAL_TYPE_STAP_A		= 24,
    NAL_TYPE_FU_A		= 28,
};


/*
 * Find next NAL unit from the specified H.264 bitstream data.
 */
static pj_uint8_t* find_next_nal_unit(pj_uint8_t *start,
                                      pj_uint8_t *end)
{
    pj_uint8_t *p = start;

    /* Simply lookup "0x000001" pattern */
    while (p <= end-3 && (p[0] || p[1] || p[2]!=1))
        ++p;

    if (p > end-3)
	/* No more NAL unit in this bitstream */
        return NULL;

    /* Include 8 bits leading zero */
    if (p>start && *(p-1)==0)
	return (p-1);

    return p;
}


/*
 * Create H264 packetizer.
 */
PJ_DEF(pj_status_t) pjmedia_h264_packetizer_create(
				pj_pool_t *pool,
				const pjmedia_h264_packetizer_cfg *cfg,
				pjmedia_h264_packetizer **p)
{
    pjmedia_h264_packetizer *p_;

    PJ_ASSERT_RETURN(pool && p, PJ_EINVAL);

    if (cfg &&
	cfg->mode != PJMEDIA_H264_PACKETIZER_MODE_NON_INTERLEAVED &&
	cfg->mode != PJMEDIA_H264_PACKETIZER_MODE_SINGLE_NAL)
    {
	return PJ_ENOTSUP;
    }

    p_ = PJ_POOL_ZALLOC_T(pool, pjmedia_h264_packetizer);
    if (cfg) {
	pj_memcpy(&p_->cfg, cfg, sizeof(*cfg));
    } else {
	p_->cfg.mode = PJMEDIA_H264_PACKETIZER_MODE_NON_INTERLEAVED;
	p_->cfg.mtu = PJMEDIA_MAX_VID_PAYLOAD_SIZE;
    }

    *p = p_;

    return PJ_SUCCESS;
}



/*
 * Generate an RTP payload from H.264 frame bitstream, in-place processing.
 */
PJ_DEF(pj_status_t) pjmedia_h264_packetize(pjmedia_h264_packetizer *pktz,
					   pj_uint8_t *buf,
                                           pj_size_t buf_len,
                                           unsigned *pos,
                                           const pj_uint8_t **payload,
                                           pj_size_t *payload_len)
{
    pj_uint8_t *nal_start = NULL, *nal_end = NULL, *nal_octet = NULL;
    pj_uint8_t *p, *end;
    enum { 
	HEADER_SIZE_FU_A	     = 2,
	HEADER_SIZE_STAP_A	     = 3,
    };
    enum { MAX_NALS_IN_AGGR = 32 };

#if DBG_PACKETIZE
    if (*pos == 0 && buf_len) {
	PJ_LOG(3, ("h264pack", "<< Start packing new frame >>"));
    }
#endif

    p = buf + *pos;
    end = buf + buf_len;

    /* Find NAL unit startcode */
    if (end-p >= 4)
	nal_start = find_next_nal_unit(p, p+4);
    if (nal_start) {
	/* Get NAL unit octet pointer */
	while (*nal_start++ == 0);
	nal_octet = nal_start;
    } else {
	/* This NAL unit is being fragmented */
	nal_start = p;
    }

    /* Get end of NAL unit */
    p = nal_start+pktz->cfg.mtu+1;
    if (p > end || pktz->cfg.mode==PJMEDIA_H264_PACKETIZER_MODE_SINGLE_NAL) 
	p = end;
    nal_end = find_next_nal_unit(nal_start, p); 
    if (!nal_end)
	nal_end = p;

    /* Validate MTU vs NAL length on single NAL unit packetization */
    if ((pktz->cfg.mode==PJMEDIA_H264_PACKETIZER_MODE_SINGLE_NAL) &&
	nal_end - nal_start > pktz->cfg.mtu)
    {
	//pj_assert(!"MTU too small for H.264 single NAL packetization mode");
	PJ_LOG(2,("h264_packetizer.c",
		  "MTU too small for H.264 (required=%u, MTU=%u)",
		  nal_end - nal_start, pktz->cfg.mtu));
	return PJ_ETOOSMALL;
    }

    /* Evaluate the proper payload format structure */

    /* Fragmentation (FU-A) packet */
    if ((pktz->cfg.mode != PJMEDIA_H264_PACKETIZER_MODE_SINGLE_NAL) &&
	(!nal_octet || nal_end-nal_start > pktz->cfg.mtu))
    {
	pj_uint8_t NRI, TYPE;

	if (nal_octet) {
	    /* We have NAL unit octet, so this is the first fragment */
	    NRI = (*nal_octet & 0x60) >> 5;
	    TYPE = *nal_octet & 0x1F;

	    /* Skip nal_octet in nal_start to be overriden by FU header */
	    ++nal_start;
	} else {
	    /* Not the first fragment, get NRI and NAL unit type
	     * from the previous fragment.
	     */
	    p = nal_start - pktz->cfg.mtu;
	    NRI = (*p & 0x60) >> 5;
	    TYPE = *(p+1) & 0x1F;
	}

	/* Init FU indicator (one octet: F+NRI+TYPE) */
	p = nal_start - HEADER_SIZE_FU_A;
	*p = (NRI << 5) | NAL_TYPE_FU_A;
	++p;

	/* Init FU header (one octed: S+E+R+TYPE) */
	*p = TYPE;
	if (nal_octet)
	    *p |= (1 << 7); /* S bit flag = start of fragmentation */
	if (nal_end-nal_start+HEADER_SIZE_FU_A <= pktz->cfg.mtu)
	    *p |= (1 << 6); /* E bit flag = end of fragmentation */

	/* Set payload, payload length */
	*payload = nal_start - HEADER_SIZE_FU_A;
	if (nal_end-nal_start+HEADER_SIZE_FU_A > pktz->cfg.mtu)
	    *payload_len = pktz->cfg.mtu;
	else
	    *payload_len = nal_end - nal_start + HEADER_SIZE_FU_A;
	*pos = *payload + *payload_len - buf;

#if DBG_PACKETIZE
	PJ_LOG(3, ("h264pack", "Packetized fragmented H264 NAL unit "
		   "(pos=%d, type=%d, NRI=%d, S=%d, E=%d, len=%d/%d)",
		   *payload-buf, TYPE, NRI, *p>>7, (*p>>6)&1, *payload_len,
		   buf_len));
#endif

	return PJ_SUCCESS;
    }

    /* Aggregation (STAP-A) packet */
    if ((pktz->cfg.mode != PJMEDIA_H264_PACKETIZER_MODE_SINGLE_NAL) &&
	(nal_end != end) &&
	(nal_end - nal_start + HEADER_SIZE_STAP_A) < pktz->cfg.mtu) 
    {
	int total_size;
	unsigned nal_cnt = 1;
	pj_uint8_t *nal[MAX_NALS_IN_AGGR];
	pj_size_t nal_size[MAX_NALS_IN_AGGR];
	pj_uint8_t NRI;

	pj_assert(nal_octet);

	/* Init the first NAL unit in the packet */
	nal[0] = nal_start;
	nal_size[0] = nal_end - nal_start;
	total_size = nal_size[0] + HEADER_SIZE_STAP_A;
	NRI = (*nal_octet & 0x60) >> 5;

	/* Populate next NAL units */
	while (nal_cnt < MAX_NALS_IN_AGGR) {
	    pj_uint8_t *tmp_end;

	    /* Find start address of the next NAL unit */
	    p = nal[nal_cnt-1] + nal_size[nal_cnt-1];
	    while (*p++ == 0);
	    nal[nal_cnt] = p;

	    /* Find end address of the next NAL unit */
	    tmp_end = p + (pktz->cfg.mtu - total_size);
	    if (tmp_end > end)
		tmp_end = end;
	    p = find_next_nal_unit(p+1, tmp_end);
	    if (p) {
		nal_size[nal_cnt] = p - nal[nal_cnt];
	    } else {
		break;
	    }

	    /* Update total payload size (2 octet NAL size + NAL) */
	    total_size += (2 + nal_size[nal_cnt]);
	    if (total_size <= pktz->cfg.mtu) {
		pj_uint8_t tmp_nri;

		/* Get maximum NRI of the aggregated NAL units */
		tmp_nri = (*(nal[nal_cnt]-1) & 0x60) >> 5;
		if (tmp_nri > NRI)
		    NRI = tmp_nri;
	    } else {
		break;
	    }

	    ++nal_cnt;
	}

	/* Only use STAP-A when we found more than one NAL units */
	if (nal_cnt > 1) {
	    unsigned i;

	    /* Init STAP-A NAL header (F+NRI+TYPE) */
	    p = nal[0] - HEADER_SIZE_STAP_A;
	    *p++ = (NRI << 5) | NAL_TYPE_STAP_A;

	    /* Append all populated NAL units into payload (SIZE+NAL) */
	    for (i = 0; i < nal_cnt; ++i) {
		/* Put size (2 octets in network order) */
		pj_assert(nal_size[i] <= 0xFFFF);
		*p++ = (pj_uint8_t)(nal_size[i] >> 8);
		*p++ = (pj_uint8_t)(nal_size[i] & 0xFF);
		
		/* Append NAL unit, watchout memmove()-ing bitstream! */
		if (p != nal[i])
		    pj_memmove(p, nal[i], nal_size[i]);
		p += nal_size[i];
	    }

	    /* Set payload, payload length, and pos */
	    *payload = nal[0] - HEADER_SIZE_STAP_A;
	    pj_assert(*payload >= buf+*pos);
	    *payload_len = p - *payload;
	    *pos = nal[nal_cnt-1] + nal_size[nal_cnt-1] - buf;

#if DBG_PACKETIZE
	    PJ_LOG(3, ("h264pack", "Packetized aggregation of "
		       "%d H264 NAL units (pos=%d, NRI=%d len=%d/%d)",
		       nal_cnt, *payload-buf, NRI, *payload_len, buf_len));
#endif

	    return PJ_SUCCESS;
	}
    }

    /* Single NAL unit packet */
    *payload = nal_start;
    *payload_len = nal_end - nal_start;
    *pos = nal_end - buf;

#if DBG_PACKETIZE
    PJ_LOG(3, ("h264pack", "Packetized single H264 NAL unit "
	       "(pos=%d, type=%d, NRI=%d, len=%d/%d)",
	       nal_start-buf, *nal_octet&0x1F, (*nal_octet&0x60)>>5,
	       *payload_len, buf_len));
#endif

    return PJ_SUCCESS;
}


/*
 * Append RTP payload to a H.264 picture bitstream. Note that the only
 * payload format that cares about packet lost is the NAL unit
 * fragmentation format (FU-A/B), so we will only manage the "prev_lost"
 * state for the FU-A/B packets.
 */
PJ_DEF(pj_status_t) pjmedia_h264_unpacketize(pjmedia_h264_packetizer *pktz,
					     const pj_uint8_t *payload,
                                             pj_size_t   payload_len,
                                             pj_uint8_t *bits,
                                             pj_size_t   bits_len,
					     unsigned   *bits_pos)
{
    const pj_uint8_t nal_start_code[3] = {0, 0, 1};
    enum { MIN_PAYLOAD_SIZE = 2 };
    pj_uint8_t nal_type;

    PJ_UNUSED_ARG(pktz);

#if DBG_UNPACKETIZE
    if (*bits_pos == 0 && payload_len) {
	PJ_LOG(3, ("h264unpack", ">> Start unpacking new frame <<"));
    }
#endif

    /* Check if this is a missing/lost packet */
    if (payload == NULL) {
	pktz->unpack_prev_lost = PJ_TRUE;
	return PJ_SUCCESS;
    }

    /* H264 payload size */
    if (payload_len < MIN_PAYLOAD_SIZE) {
	/* Invalid bitstream, discard this payload */
	pktz->unpack_prev_lost = PJ_TRUE;
	return PJ_EINVAL;
    }

    /* Reset last sync point for every new picture bitstream */
    if (*bits_pos == 0)
	pktz->unpack_last_sync_pos = 0;

    nal_type = *payload & 0x1F;
    if (nal_type >= NAL_TYPE_SINGLE_NAL_MIN &&
	nal_type <= NAL_TYPE_SINGLE_NAL_MAX)
    {
	/* Single NAL unit packet */
	pj_uint8_t *p = bits + *bits_pos;

	/* Validate bitstream length */
	if (bits_len-*bits_pos < payload_len+PJ_ARRAY_SIZE(nal_start_code)) {
	    /* Insufficient bistream buffer, discard this payload */
	    pj_assert(!"Insufficient H.263 bitstream buffer");
	    return PJ_ETOOSMALL;
	}

	/* Write NAL unit start code */
	pj_memcpy(p, &nal_start_code, PJ_ARRAY_SIZE(nal_start_code));
	p += PJ_ARRAY_SIZE(nal_start_code);

	/* Write NAL unit */
	pj_memcpy(p, payload, payload_len);
	p += payload_len;

	/* Update the bitstream writing offset */
	*bits_pos = p - bits;
	pktz->unpack_last_sync_pos = *bits_pos;

#if DBG_UNPACKETIZE
	PJ_LOG(3, ("h264unpack", "Unpacked single H264 NAL unit "
		   "(type=%d, NRI=%d, len=%d)",
		   nal_type, (*payload&0x60)>>5, payload_len));
#endif

    }
    else if (nal_type == NAL_TYPE_STAP_A)
    {
	/* Aggregation packet */
	pj_uint8_t *p, *p_end;
	const pj_uint8_t *q, *q_end;
	unsigned cnt = 0;

	/* Validate bitstream length */
	if (bits_len - *bits_pos < payload_len + 32) {
	    /* Insufficient bistream buffer, discard this payload */
	    pj_assert(!"Insufficient H.263 bitstream buffer");
	    return PJ_ETOOSMALL;
	}

	/* Fill bitstream */
	p = bits + *bits_pos;
	p_end = bits + bits_len;
	q = payload + 1;
	q_end = payload + payload_len;
	while (q < q_end && p < p_end) {
	    pj_uint16_t tmp_nal_size;

	    /* Write NAL unit start code */
	    pj_memcpy(p, &nal_start_code, PJ_ARRAY_SIZE(nal_start_code));
	    p += PJ_ARRAY_SIZE(nal_start_code);

	    /* Get NAL unit size */
	    tmp_nal_size = (*q << 8) | *(q+1);
	    q += 2;
	    if (q + tmp_nal_size > q_end) {
		/* Invalid bitstream, discard the rest of the payload */
		return PJ_EINVAL;
	    }

	    /* Write NAL unit */
	    pj_memcpy(p, q, tmp_nal_size);
	    p += tmp_nal_size;
	    q += tmp_nal_size;
	    ++cnt;

	    /* Update the bitstream writing offset */
	    *bits_pos = p - bits;
	    pktz->unpack_last_sync_pos = *bits_pos;
	}

#if DBG_UNPACKETIZE
	PJ_LOG(3, ("h264unpack", "Unpacked %d H264 NAL units (len=%d)",
		   cnt, payload_len));
#endif

    }
    else if (nal_type == NAL_TYPE_FU_A)
    {
	/* Fragmentation packet */
	pj_uint8_t *p;
	const pj_uint8_t *q = payload;
	pj_uint8_t NRI, TYPE, S, E;

	p = bits + *bits_pos;

	/* Validate bitstream length */
	if (bits_len-*bits_pos < payload_len+PJ_ARRAY_SIZE(nal_start_code)) {
	    /* Insufficient bistream buffer, drop this packet */
	    pj_assert(!"Insufficient H.263 bitstream buffer");
	    pktz->unpack_prev_lost = PJ_TRUE;
	    return PJ_ETOOSMALL;
	}

	/* Get info */
	S = *(q+1) & 0x80;    /* Start bit flag	*/
	E = *(q+1) & 0x40;    /* End bit flag	*/
	TYPE = *(q+1) & 0x1f;
	NRI = (*q & 0x60) >> 5;

	/* Fill bitstream */
	if (S) {
	    /* This is the first part, write NAL unit start code */
	    pj_memcpy(p, &nal_start_code, PJ_ARRAY_SIZE(nal_start_code));
	    p += PJ_ARRAY_SIZE(nal_start_code);

	    /* Write NAL unit octet */
	    *p++ = (NRI << 5) | TYPE;
	} else if (pktz->unpack_prev_lost) {
	    /* If prev packet was lost, revert the bitstream pointer to
	     * the last sync point.
	     */
	    pj_assert(pktz->unpack_last_sync_pos <= *bits_pos);
	    *bits_pos = pktz->unpack_last_sync_pos;
	    /* And discard this payload (and the following fragmentation
	     * payloads carrying this same NAL unit.
	     */
	    return PJ_EIGNORED;
	}
	q += 2;

	/* Write NAL unit */
	pj_memcpy(p, q, payload_len - 2);
	p += (payload_len - 2);

	/* Update the bitstream writing offset */
	*bits_pos = p - bits;
	if (E) {
	    /* Update the sync pos only if the end bit flag is set */
	    pktz->unpack_last_sync_pos = *bits_pos;
	}

#if DBG_UNPACKETIZE
	PJ_LOG(3, ("h264unpack", "Unpacked fragmented H264 NAL unit "
		   "(type=%d, NRI=%d, len=%d)",
		   TYPE, NRI, payload_len));
#endif

    } else {
	*bits_pos = 0;
	return PJ_ENOTSUP;
    }

    pktz->unpack_prev_lost = PJ_FALSE;

    return PJ_SUCCESS;
}


#endif /* PJMEDIA_HAS_VIDEO */
