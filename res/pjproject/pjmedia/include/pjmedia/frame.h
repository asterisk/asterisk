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
#ifndef __PJMEDIA_FRAME_H__
#define __PJMEDIA_FRAME_H__

/**
 * @file pjmedia/frame.h Media frame
 * @brief Frame
 */
#include <pjmedia/types.h>
#include <pj/string.h>

/**
 * @defgroup PJMEDIA_FRAME Media frame
 * @ingroup PJMEDIA_TYPES
 * @brief Frame
 * @{
 */

PJ_BEGIN_DECL


/**
 * Types of media frame.
 */
typedef enum pjmedia_frame_type
{
    PJMEDIA_FRAME_TYPE_NONE,	    /**< No frame.		*/
    PJMEDIA_FRAME_TYPE_AUDIO,	    /**< Normal audio frame.	*/
    PJMEDIA_FRAME_TYPE_EXTENDED,    /**< Extended audio frame.	*/
    PJMEDIA_FRAME_TYPE_VIDEO        /**< Video frame.           */

} pjmedia_frame_type;


/**
 * This structure describes a media frame.
 */
typedef struct pjmedia_frame
{
    pjmedia_frame_type	 type;	    /**< Frame type.			    */
    void		*buf;	    /**< Pointer to buffer.		    */
    pj_size_t		 size;	    /**< Frame size in bytes.		    */
    pj_timestamp	 timestamp; /**< Frame timestamp.		    */
    pj_uint32_t		 bit_info;  /**< Bit info of the frame, sample case:
					 a frame may not exactly start and end
					 at the octet boundary, so this field
					 may be used for specifying start &
					 end bit offset.		    */
} pjmedia_frame;


/**
 * The pjmedia_frame_ext is used to carry a more complex audio frames than
 * the typical PCM audio frames, and it is signaled by setting the "type"
 * field of a pjmedia_frame to PJMEDIA_FRAME_TYPE_EXTENDED. With this set,
 * application may typecast pjmedia_frame to pjmedia_frame_ext.
 *
 * This structure may contain more than one audio frames, which subsequently
 * will be called subframes in this structure. The subframes section
 * immediately follows the end of this structure, and each subframe is
 * represented by pjmedia_frame_ext_subframe structure. Every next
 * subframe immediately follows the previous subframe, and all subframes
 * are byte-aligned although its payload may not be byte-aligned.
 */

#pragma pack(1)
typedef struct pjmedia_frame_ext {
    pjmedia_frame   base;	    /**< Base frame info */
    pj_uint16_t     samples_cnt;    /**< Number of samples in this frame */
    pj_uint16_t     subframe_cnt;   /**< Number of (sub)frames in this frame */

    /* Zero or more (sub)frames follows immediately after this,
     * each will be represented by pjmedia_frame_ext_subframe
     */
} pjmedia_frame_ext;
#pragma pack()

/**
 * This structure represents the individual subframes in the
 * pjmedia_frame_ext structure.
 */
#pragma pack(1)
typedef struct pjmedia_frame_ext_subframe {
    pj_uint16_t     bitlen;	    /**< Number of bits in the data */
    pj_uint8_t      data[1];	    /**< Start of encoded data */
} pjmedia_frame_ext_subframe;

#pragma pack()

/**
 * Copy one frame to another. If the destination frame's size is smaller than
 * the source frame's, the destination buffer will be truncated.
 *
 * @param src		    Source frame.
 * @param dst		    Destination frame.
 */
PJ_INLINE(void) pjmedia_frame_copy(pjmedia_frame *dst,
				   const pjmedia_frame *src)
{
    dst->type = src->type;
    dst->timestamp = src->timestamp;
    dst->bit_info = src->bit_info;
    dst->size = (dst->size < src->size? dst->size: src->size);
    pj_memcpy(dst->buf, src->buf, dst->size);
}

/**
 * Append one subframe to #pjmedia_frame_ext.
 *
 * @param frm		    The #pjmedia_frame_ext.
 * @param src		    Subframe data.
 * @param bitlen	    Length of subframe, in bits.
 * @param samples_cnt	    Number of audio samples in subframe.
 */
PJ_INLINE(void) pjmedia_frame_ext_append_subframe(pjmedia_frame_ext *frm,
						  const void *src,
					          unsigned bitlen,
						  unsigned samples_cnt)
{
    pjmedia_frame_ext_subframe *fsub;
    pj_uint8_t *p;
    unsigned i;

    p = (pj_uint8_t*)frm + sizeof(pjmedia_frame_ext);
    for (i = 0; i < frm->subframe_cnt; ++i) {
	fsub = (pjmedia_frame_ext_subframe*) p;
	p += sizeof(fsub->bitlen) + ((fsub->bitlen+7) >> 3);
    }

    fsub = (pjmedia_frame_ext_subframe*) p;
    fsub->bitlen = (pj_uint16_t)bitlen;
    if (bitlen)
	pj_memcpy(fsub->data, src, (bitlen+7) >> 3);

    frm->subframe_cnt++;
    frm->samples_cnt = (pj_uint16_t)(frm->samples_cnt + samples_cnt);
}

/**
 * Get a subframe from #pjmedia_frame_ext.
 *
 * @param frm		    The #pjmedia_frame_ext.
 * @param n		    Subframe index, zero based.
 *
 * @return		    The n-th subframe, or NULL if n is out-of-range.
 */
PJ_INLINE(pjmedia_frame_ext_subframe*)
pjmedia_frame_ext_get_subframe(const pjmedia_frame_ext *frm, unsigned n)
{
    pjmedia_frame_ext_subframe *sf = NULL;

    if (n < frm->subframe_cnt) {
	pj_uint8_t *p;
	unsigned i;

	p = (pj_uint8_t*)frm + sizeof(pjmedia_frame_ext);
	for (i = 0; i < n; ++i) {
	    sf = (pjmedia_frame_ext_subframe*) p;
	    p += sizeof(sf->bitlen) + ((sf->bitlen+7) >> 3);
	}

	sf = (pjmedia_frame_ext_subframe*) p;
    }

    return sf;
}

/**
 * Extract all frame payload to the specified buffer.
 *
 * @param frm		    The frame.
 * @param dst		    Destination buffer.
 * @param maxlen	    Maximum size to copy (i.e. the size of the
 *			    destination buffer).
 *
 * @return		    Total size of payload copied.
 */
PJ_INLINE(unsigned)
pjmedia_frame_ext_copy_payload(const pjmedia_frame_ext *frm,
			       void *dst,
			       unsigned maxlen)
{
    unsigned i, copied=0;
    for (i=0; i<frm->subframe_cnt; ++i) {
	pjmedia_frame_ext_subframe *sf;
	unsigned sz;

	sf = pjmedia_frame_ext_get_subframe(frm, i);
	if (!sf)
	    continue;

	sz = ((sf->bitlen + 7) >> 3);
	if (sz + copied > maxlen)
	    break;

	pj_memcpy(((pj_uint8_t*)dst) + copied, sf->data, sz);
	copied += sz;
    }
    return copied;
}


/**
 * Pop out first n subframes from #pjmedia_frame_ext.
 *
 * @param frm		    The #pjmedia_frame_ext.
 * @param n		    Number of first subframes to be popped out.
 *
 * @return		    PJ_SUCCESS when successful.
 */
PJ_INLINE(pj_status_t)
pjmedia_frame_ext_pop_subframes(pjmedia_frame_ext *frm, unsigned n)
{
    pjmedia_frame_ext_subframe *sf;
    pj_uint8_t *move_src;
    unsigned move_len;

    if (frm->subframe_cnt <= n) {
	frm->subframe_cnt = 0;
	frm->samples_cnt = 0;
	return PJ_SUCCESS;
    }

    move_src = (pj_uint8_t*)pjmedia_frame_ext_get_subframe(frm, n);
    sf = pjmedia_frame_ext_get_subframe(frm, frm->subframe_cnt-1);
    move_len = (pj_uint8_t*)sf - move_src + sizeof(sf->bitlen) +
	       ((sf->bitlen+7) >> 3);
    pj_memmove((pj_uint8_t*)frm+sizeof(pjmedia_frame_ext),
	       move_src, move_len);

    frm->samples_cnt = (pj_uint16_t)
		   (frm->samples_cnt - n*frm->samples_cnt/frm->subframe_cnt);
    frm->subframe_cnt = (pj_uint16_t) (frm->subframe_cnt - n);

    return PJ_SUCCESS;
}


/**
 * This is a general purpose function set PCM samples to zero.
 * Since this function is needed by many parts of the library,
 * by putting this functionality in one place, it enables some.
 * clever people to optimize this function.
 *
 * @param samples	The 16bit PCM samples.
 * @param count		Number of samples.
 */
PJ_INLINE(void) pjmedia_zero_samples(pj_int16_t *samples, unsigned count)
{
#if 1
    pj_bzero(samples, (count<<1));
#elif 0
    unsigned i;
    for (i=0; i<count; ++i) samples[i] = 0;
#else
    unsigned i;
    count >>= 1;
    for (i=0; i<count; ++i) ((pj_int32_t*)samples)[i] = (pj_int32_t)0;
#endif
}


/**
 * This is a general purpose function to copy samples from/to buffers with
 * equal size. Since this function is needed by many parts of the library,
 * by putting this functionality in one place, it enables some.
 * clever people to optimize this function.
 */
PJ_INLINE(void) pjmedia_copy_samples(pj_int16_t *dst, const pj_int16_t *src,
				     unsigned count)
{
#if 1
    pj_memcpy(dst, src, (count<<1));
#elif 0
    unsigned i;
    for (i=0; i<count; ++i) dst[i] = src[i];
#else
    unsigned i;
    count >>= 1;
    for (i=0; i<count; ++i)
	((pj_int32_t*)dst)[i] = ((pj_int32_t*)src)[i];
#endif
}


/**
 * This is a general purpose function to copy samples from/to buffers with
 * equal size. Since this function is needed by many parts of the library,
 * by putting this functionality in one place, it enables some.
 * clever people to optimize this function.
 */
PJ_INLINE(void) pjmedia_move_samples(pj_int16_t *dst, const pj_int16_t *src,
				     unsigned count)
{
#if 1
    pj_memmove(dst, src, (count<<1));
#elif 0
    unsigned i;
    for (i=0; i<count; ++i) dst[i] = src[i];
#else
    unsigned i;
    count >>= 1;
    for (i=0; i<count; ++i)
	((pj_int32_t*)dst)[i] = ((pj_int32_t*)src)[i];
#endif
}

PJ_END_DECL

/**
 * @}
 */

#endif /* __PJMEDIA_FRAME_H__ */
