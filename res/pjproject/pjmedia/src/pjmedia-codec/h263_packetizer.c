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
#include <pjmedia-codec/h263_packetizer.h>
#include <pjmedia/types.h>
#include <pj/assert.h>
#include <pj/errno.h>
#include <pj/string.h>


#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)


#define THIS_FILE	"h263_packetizer.c"


/* H.263 packetizer definition */
struct pjmedia_h263_packetizer {
    /* Current settings */
    pjmedia_h263_packetizer_cfg cfg;
    
    /* Unpacketizer state */
    unsigned	    unpack_last_sync_pos;
    pj_bool_t	    unpack_prev_lost;
};


/*
 * Find synchronization point (PSC, slice, GSBC, EOS, EOSBS) in H.263 
 * bitstream.
 */
static pj_uint8_t* find_sync_point(pj_uint8_t *data,
				   pj_size_t data_len)
{
    pj_uint8_t *p = data, *end = data+data_len-1;

    while (p < end && (*p || *(p+1)))
        ++p;

    if (p == end)
        return NULL;
        
    return p;
}


/*
 * Find synchronization point (PSC, slice, GSBC, EOS, EOSBS) in H.263 
 * bitstream, in reversed manner.
 */
static pj_uint8_t* find_sync_point_rev(pj_uint8_t *data,
                                       pj_size_t data_len)
{
    pj_uint8_t *p = data+data_len-2;

    while (p >= data && (*p || *(p+1)))
        --p;

    if (p < data)
        return (data + data_len);
        
    return p;
}


/*
 * Create H263 packetizer.
 */
PJ_DEF(pj_status_t) pjmedia_h263_packetizer_create(
				pj_pool_t *pool,
				const pjmedia_h263_packetizer_cfg *cfg,
				pjmedia_h263_packetizer **p)
{
    pjmedia_h263_packetizer *p_;

    PJ_ASSERT_RETURN(pool && p, PJ_EINVAL);

    if (cfg && cfg->mode != PJMEDIA_H263_PACKETIZER_MODE_RFC4629)
	return PJ_ENOTSUP;

    p_ = PJ_POOL_ZALLOC_T(pool, pjmedia_h263_packetizer);
    if (cfg) {
	pj_memcpy(&p_->cfg, cfg, sizeof(*cfg));
    } else {
	p_->cfg.mode = PJMEDIA_H263_PACKETIZER_MODE_RFC4629;
	p_->cfg.mtu = PJMEDIA_MAX_VID_PAYLOAD_SIZE;
    }

    *p = p_;

    return PJ_SUCCESS;
}


/*
 * Generate an RTP payload from H.263 frame bitstream, in-place processing.
 */
PJ_DEF(pj_status_t) pjmedia_h263_packetize(pjmedia_h263_packetizer *pktz,
					   pj_uint8_t *bits,
                                           pj_size_t bits_len,
                                           unsigned *pos,
                                           const pj_uint8_t **payload,
                                           pj_size_t *payload_len)
{
    pj_uint8_t *p, *end;

    pj_assert(pktz && bits && pos && payload && payload_len);
    pj_assert(*pos <= bits_len);

    p = bits + *pos;
    end = bits + bits_len;

    /* Put two octets payload header */
    if ((end-p > 2) && *p==0 && *(p+1)==0) {
        /* The bitstream starts with synchronization point, just override
         * the two zero octets (sync point mark) for payload header.
         */
        *p = 0x04;
    } else {
        /* Not started in synchronization point, we will use two octets
         * preceeding the bitstream for payload header!
         */

	if (*pos < 2) {
	    /* Invalid H263 bitstream, it's not started with PSC */
	    return PJ_EINVAL;
	}

	p -= 2;
        *p = 0;
    }
    *(p+1) = 0;

    /* When bitstream truncation needed because of payload length/MTU 
     * limitation, try to use sync point for the payload boundary.
     */
    if (end-p > pktz->cfg.mtu) {
	end = find_sync_point_rev(p+2, pktz->cfg.mtu-2);
    }

    *payload = p;
    *payload_len = end-p;
    *pos = end - bits;

    return PJ_SUCCESS;
}


/*
 * Append an RTP payload to a H.263 picture bitstream.
 */
PJ_DEF(pj_status_t) pjmedia_h263_unpacketize (pjmedia_h263_packetizer *pktz,
					      const pj_uint8_t *payload,
                                              pj_size_t payload_len,
                                              pj_uint8_t *bits,
                                              pj_size_t bits_size,
					      unsigned *pos)
{
    pj_uint8_t P, V, PLEN;
    const pj_uint8_t *p = payload;
    pj_uint8_t *q;

    q = bits + *pos;

    /* Check if this is a missing/lost packet */
    if (payload == NULL) {
	pktz->unpack_prev_lost = PJ_TRUE;
	return PJ_SUCCESS;
    }

    /* H263 payload header size is two octets */
    if (payload_len < 2) {
	/* Invalid bitstream, discard this payload */
	pktz->unpack_prev_lost = PJ_TRUE;
	return PJ_EINVAL;
    }

    /* Reset last sync point for every new picture bitstream */
    if (*pos == 0)
	pktz->unpack_last_sync_pos = 0;

    /* Get payload header info */
    P = *p & 0x04;
    V = *p & 0x02;
    PLEN = ((*p & 0x01) << 5) + ((*(p+1) & 0xF8)>>3);

    /* Get start bitstream pointer */
    p += 2;	    /* Skip payload header */
    if (V)
        p += 1;	    /* Skip VRC data */
    if (PLEN)
        p += PLEN;  /* Skip extra picture header data */

    /* Get bitstream length */
    if (payload_len > (pj_size_t)(p - payload)) {
	payload_len -= (p - payload);
    } else {
	/* Invalid bitstream, discard this payload */
	pktz->unpack_prev_lost = PJ_TRUE;
	return PJ_EINVAL;
    }

    /* Validate bitstream length */
    if (bits_size < *pos + payload_len + 2) {
	/* Insufficient bistream buffer, discard this payload */
	pj_assert(!"Insufficient H.263 bitstream buffer");
	pktz->unpack_prev_lost = PJ_TRUE;
	return PJ_ETOOSMALL;
    }

    /* Start writing bitstream */

    /* No sync point flag */
    if (!P) {
	if (*pos == 0) {
	    /* Previous packet must be lost */
	    pktz->unpack_prev_lost = PJ_TRUE;

	    /* If there is extra picture header, let's use it. */
	    if (PLEN) {
		/* Write two zero octets for PSC */
		*q++ = 0;
		*q++ = 0;
		/* Copy the picture header */
		p -= PLEN;
		pj_memcpy(q, p, PLEN);
		p += PLEN;
		q += PLEN;
	    }
	} else if (pktz->unpack_prev_lost) {
	    /* If prev packet was lost, revert the bitstream pointer to
	     * the last sync point.
	     */
	    pj_assert(pktz->unpack_last_sync_pos <= *pos);
	    q = bits + pktz->unpack_last_sync_pos;
	}

	/* There was packet lost, see if this payload contain sync point
	 * (usable data).
	 */
	if (pktz->unpack_prev_lost) {
	    pj_uint8_t *sync;
	    sync = find_sync_point((pj_uint8_t*)p, payload_len);
	    if (sync) {
		/* Got sync point, update P/sync-point flag */
		P = 1;
		/* Skip the two zero octets */
		sync += 2;
		/* Update payload length and start bitstream pointer */
		payload_len -= (sync - p);
		p = sync;
	    } else {
		/* No sync point in it, just discard this payload */
		return PJ_EIGNORED;
	    }
	}
    }

    /* Write two zero octets when payload flagged with sync point */
    if (P) {
	pktz->unpack_last_sync_pos = q - bits;
        *q++ = 0;
        *q++ = 0;
    }

    /* Write the payload to the bitstream */
    pj_memcpy(q, p, payload_len);
    q += payload_len;

    /* Update the bitstream writing offset */
    *pos = q - bits;

    pktz->unpack_prev_lost = PJ_FALSE;

    return PJ_SUCCESS;
}


#endif /* PJMEDIA_HAS_VIDEO */
