/* $Id$ */
/*
 * Copyright (C) 2010-2011 Teluu Inc. (http://www.teluu.com)
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
#include <pjmedia/types.h>
#include <pj/errno.h>
#include <pj/log.h>
#include <pj/string.h>

#if PJMEDIA_HAS_LIBAVFORMAT && PJMEDIA_HAS_LIBAVUTIL

#include "ffmpeg_util.h"
#include <libavformat/avformat.h>

#define MAKE_VER(mj,mn,mi)	((mj << 16) | (mn << 8) | (mi << 0))
#define VER_AT_LEAST(mj,mn,mi)	(MAKE_VER(LIBAVUTIL_VERSION_MAJOR, \
                              	          LIBAVUTIL_VERSION_MINOR, \
                              	          LIBAVUTIL_VERSION_MICRO) >= \
                              	 MAKE_VER(mj,mn,mi))



/* Conversion table between pjmedia_format_id and PixelFormat */
static const struct ffmpeg_fmt_table_t
{
    pjmedia_format_id	id;
    enum PixelFormat	pf;
} ffmpeg_fmt_table[] =
{
    { PJMEDIA_FORMAT_RGBA, PIX_FMT_RGBA},
    { PJMEDIA_FORMAT_RGB24,PIX_FMT_BGR24},
    { PJMEDIA_FORMAT_BGRA, PIX_FMT_BGRA},
#if VER_AT_LEAST(51,20,1)
    { PJMEDIA_FORMAT_GBRP, PIX_FMT_GBR24P},
#endif

    { PJMEDIA_FORMAT_AYUV, PIX_FMT_NONE},
    { PJMEDIA_FORMAT_YUY2, PIX_FMT_YUYV422},
    { PJMEDIA_FORMAT_UYVY, PIX_FMT_UYVY422},
    { PJMEDIA_FORMAT_I420, PIX_FMT_YUV420P},
    { PJMEDIA_FORMAT_YV12, PIX_FMT_YUV422P},
    { PJMEDIA_FORMAT_I420JPEG, PIX_FMT_YUVJ420P},
    { PJMEDIA_FORMAT_I422JPEG, PIX_FMT_YUVJ422P},
};

/* Conversion table between pjmedia_format_id and CodecID */
static const struct ffmpeg_codec_table_t
{
    pjmedia_format_id	id;
    enum CodecID	codec_id;
} ffmpeg_codec_table[] =
{
    {PJMEDIA_FORMAT_H261,	CODEC_ID_H261},
    {PJMEDIA_FORMAT_H263,	CODEC_ID_H263},
    {PJMEDIA_FORMAT_H263P,	CODEC_ID_H263P},
    {PJMEDIA_FORMAT_H264,	CODEC_ID_H264},
    {PJMEDIA_FORMAT_MPEG1VIDEO,	CODEC_ID_MPEG1VIDEO},
    {PJMEDIA_FORMAT_MPEG2VIDEO, CODEC_ID_MPEG2VIDEO},
    {PJMEDIA_FORMAT_MPEG4,	CODEC_ID_MPEG4},
    {PJMEDIA_FORMAT_MJPEG,	CODEC_ID_MJPEG}
};

static int pjmedia_ffmpeg_ref_cnt;

static void ffmpeg_log_cb(void* ptr, int level, const char* fmt, va_list vl);

void pjmedia_ffmpeg_add_ref()
{
    if (pjmedia_ffmpeg_ref_cnt++ == 0) {
	av_log_set_level(AV_LOG_ERROR);
	av_log_set_callback(&ffmpeg_log_cb);
	av_register_all();
    }
}

void pjmedia_ffmpeg_dec_ref()
{
    if (pjmedia_ffmpeg_ref_cnt-- == 1) {
	/* How to shutdown ffmpeg? */
    }

    if (pjmedia_ffmpeg_ref_cnt < 0) pjmedia_ffmpeg_ref_cnt = 0;
}


static void ffmpeg_log_cb(void* ptr, int level, const char* fmt, va_list vl)
{
    const char *LOG_SENDER = "ffmpeg";
    enum { LOG_LEVEL = 5 };
    char buf[100];
    int bufsize = sizeof(buf), len;
    pj_str_t fmt_st;

    /* Custom callback needs to filter log level by itself */
    if (level > av_log_get_level())
	return;
    
    /* Add original ffmpeg sender to log format */
    if (ptr) {
	AVClass* avc = *(AVClass**)ptr;
	len = pj_ansi_snprintf(buf, bufsize, "%s: ", avc->item_name(ptr));
	bufsize -= len;
    }

    /* Copy original log format */
    len = pj_ansi_strlen(fmt);
    if (len > bufsize-1)
	len = bufsize-1;
    pj_memcpy(buf+sizeof(buf)-bufsize, fmt, len);
    bufsize -= len;

    /* Trim log format */
    pj_strset(&fmt_st, buf, sizeof(buf)-bufsize);
    pj_strrtrim(&fmt_st);
    buf[fmt_st.slen] = '\0';

    pj_log(LOG_SENDER, LOG_LEVEL, buf, vl);
}


pj_status_t pjmedia_format_id_to_PixelFormat(pjmedia_format_id fmt_id,
					     enum PixelFormat *pixel_format)
{
    unsigned i;
    for (i=0; i<PJ_ARRAY_SIZE(ffmpeg_fmt_table); ++i) {
	const struct ffmpeg_fmt_table_t *t = &ffmpeg_fmt_table[i];
	if (t->id==fmt_id && t->pf != PIX_FMT_NONE) {
	    *pixel_format = t->pf;
	    return PJ_SUCCESS;
	}
    }

    *pixel_format = PIX_FMT_NONE;
    return PJ_ENOTFOUND;
}

pj_status_t PixelFormat_to_pjmedia_format_id(enum PixelFormat pf,
					     pjmedia_format_id *fmt_id)
{
    unsigned i;
    for (i=0; i<PJ_ARRAY_SIZE(ffmpeg_fmt_table); ++i) {
	const struct ffmpeg_fmt_table_t *t = &ffmpeg_fmt_table[i];
	if (t->pf == pf) {
	    if (fmt_id) *fmt_id = t->id;
	    return PJ_SUCCESS;
	}
    }

    return PJ_ENOTFOUND;
}

pj_status_t pjmedia_format_id_to_CodecID(pjmedia_format_id fmt_id,
					 enum CodecID *codec_id)
{
    unsigned i;
    for (i=0; i<PJ_ARRAY_SIZE(ffmpeg_codec_table); ++i) {
	const struct ffmpeg_codec_table_t *t = &ffmpeg_codec_table[i];
	if (t->id==fmt_id && t->codec_id != PIX_FMT_NONE) {
	    *codec_id = t->codec_id;
	    return PJ_SUCCESS;
	}
    }

    *codec_id = PIX_FMT_NONE;
    return PJ_ENOTFOUND;
}

pj_status_t CodecID_to_pjmedia_format_id(enum CodecID codec_id,
					 pjmedia_format_id *fmt_id)
{
    unsigned i;
    for (i=0; i<PJ_ARRAY_SIZE(ffmpeg_codec_table); ++i) {
	const struct ffmpeg_codec_table_t *t = &ffmpeg_codec_table[i];
	if (t->codec_id == codec_id) {
	    if (fmt_id) *fmt_id = t->id;
	    return PJ_SUCCESS;
	}
    }

    return PJ_ENOTFOUND;
}


#ifdef _MSC_VER
#   pragma comment( lib, "avformat.lib")
#   pragma comment( lib, "avutil.lib")
#endif

#endif	/* #if PJMEDIA_HAS_LIBAVFORMAT && PJMEDIA_HAS_LIBAVUTIL */
