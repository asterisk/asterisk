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
#ifndef __PJMEDIA_AVI_H__
#define __PJMEDIA_AVI_H__


/**
 * @file avi.h
 * @brief AVI file manipulation.
 */

/**
 * @defgroup PJMEDIA_FILE_FORMAT File Formats
 * @brief Supported file formats
 */


/**
 * @defgroup PJMEDIA_AVI AVI Header
 * @ingroup PJMEDIA_FILE_FORMAT
 * @brief Representation of RIFF/AVI file format
 * @{
 *
 * This the the low level representation of RIFF/AVI file format. For
 * higher abstraction, please see \ref PJMEDIA_FILE_PLAY and 
 * \ref PJMEDIA_FILE_REC.
 */


PJ_BEGIN_DECL

#define PJMEDIA_AVI_MAX_NUM_STREAMS 4

static const char avi_tags[][4] = {
    { 'R', 'I', 'F', 'F' }, { 'A', 'V', 'I', ' ' },
    { 'h', 'd', 'r', 'l' }, { 'a', 'v', 'i', 'h' },
    { 's', 't', 'r', 'l' }, { 's', 't', 'r', 'h' },
    { 'a', 'u', 'd', 's' }, { 'v', 'i', 'd', 's' },
    { 's', 't', 'r', 'f' }, { 'm', 'o', 'v', 'i' },
    { 'L', 'I', 'S', 'T' }, { 'J', 'U', 'N', 'K' },
};

typedef enum {
    PJMEDIA_AVI_RIFF_TAG = 0,
    PJMEDIA_AVI_AVI_TAG,
    PJMEDIA_AVI_HDRL_TAG,
    PJMEDIA_AVI_AVIH_TAG,
    PJMEDIA_AVI_STRL_TAG,
    PJMEDIA_AVI_STRH_TAG,
    PJMEDIA_AVI_AUDS_TAG,
    PJMEDIA_AVI_VIDS_TAG,
    PJMEDIA_AVI_STRF_TAG,
    PJMEDIA_AVI_MOVI_TAG,
    PJMEDIA_AVI_LIST_TAG,
    PJMEDIA_AVI_JUNK_TAG,
} pjmedia_avi_tag;


/**
 * These types describe the simpler/canonical version of an AVI file.
 * They do not support the full AVI RIFF format specification.
 */
#pragma pack(2)

/** This structure describes RIFF AVI file header */
typedef struct riff_hdr_t {
    pj_uint32_t riff;		/**< "RIFF" ASCII tag.		*/
    pj_uint32_t file_len;       /**< File length minus 8 bytes	*/
    pj_uint32_t avi;		/**< "AVI" ASCII tag.		*/
} riff_hdr_t;

/** This structure describes avih header  */
typedef struct avih_hdr_t {
    pj_uint32_t list_tag;
    pj_uint32_t list_sz;
    pj_uint32_t hdrl_tag;
    pj_uint32_t avih;
    pj_uint32_t size;
    pj_uint32_t usec_per_frame;     /**< microsecs between frames   */
    pj_uint32_t max_Bps;
    pj_uint32_t pad;
    pj_uint32_t flags;
    pj_uint32_t tot_frames;
    pj_uint32_t init_frames;
    pj_uint32_t num_streams;
    pj_uint32_t buf_size;
    pj_uint32_t width;
    pj_uint32_t height;
    pj_uint32_t reserved[4];
} avih_hdr_t;

/** This structure describes strl header  */
typedef struct strl_hdr_t {
    pj_uint32_t list_tag;
    pj_uint32_t list_sz;
    pj_uint32_t strl_tag;

    pj_uint32_t strh;
    pj_uint32_t strh_size;
    pj_uint32_t data_type;
    pj_uint32_t codec;
    pj_uint32_t flags;
    pj_uint32_t bogus_priority_language; /**< Do not access this data */
    pj_uint32_t init_frames;
    pj_uint32_t scale;
    pj_uint32_t rate;
    pj_uint32_t start;
    pj_uint32_t length;
    pj_uint32_t buf_size;
    pj_uint32_t quality;
    pj_uint32_t sample_size;
    pj_uint32_t bogus_frame[2];          /**< Do not access this data */
} strl_hdr_t;

typedef struct {
    pj_uint32_t strf;
    pj_uint32_t strf_size;
    pj_uint16_t fmt_tag;	    /**< 1 for PCM			*/
    pj_uint16_t nchannels;          /**< Number of channels.	        */
    pj_uint32_t sample_rate;	    /**< Sampling rate.		        */
    pj_uint32_t bytes_per_sec;	    /**< Average bytes per second.	*/
    pj_uint16_t block_align;	    /**< nchannels * bits / 8	        */
    pj_uint16_t bits_per_sample;    /**< Bits per sample.		*/
    pj_uint16_t extra_size;
} strf_audio_hdr_t;

/**
 * Sizes of strf_audio_hdr_t struct, started by the size (in bytes) of
 * 32-bits struct members, alternated with the size of 16-bits members.
 */
static const pj_uint8_t strf_audio_hdr_sizes [] = {8, 4, 8, 6};

typedef struct {
    pj_uint32_t strf;
    pj_uint32_t strf_size;
    pj_uint32_t biSize; 
    pj_int32_t biWidth; 
    pj_int32_t biHeight; 
    pj_uint16_t biPlanes; 
    pj_uint16_t biBitCount;
    pj_uint32_t biCompression; 
    pj_uint32_t biSizeImage; 
    pj_int32_t biXPelsPerMeter; 
    pj_int32_t biYPelsPerMeter; 
    pj_uint32_t biClrUsed; 
    pj_uint32_t biClrImportant; 
} strf_video_hdr_t;

static const pj_uint8_t strf_video_hdr_sizes [] = {20, 4, 24};

struct pjmedia_avi_hdr
{
    riff_hdr_t  riff_hdr;
    avih_hdr_t  avih_hdr;
    strl_hdr_t  strl_hdr[PJMEDIA_AVI_MAX_NUM_STREAMS];
    union {
        strf_audio_hdr_t strf_audio_hdr;
        strf_video_hdr_t strf_video_hdr;
    } strf_hdr[PJMEDIA_AVI_MAX_NUM_STREAMS];
};

#pragma pack()

/**
 * @see pjmedia_avi_hdr
 */
typedef struct pjmedia_avi_hdr pjmedia_avi_hdr;

/**
 * This structure describes generic RIFF subchunk header.
 */
typedef struct pjmedia_avi_subchunk
{
    pj_uint32_t	    id;			/**< Subchunk ASCII tag.	    */
    pj_uint32_t	    len;		/**< Length following this field    */
} pjmedia_avi_subchunk;


PJ_END_DECL

/**
 * @}
 */


#endif	/* __PJMEDIA_AVI_H__ */
