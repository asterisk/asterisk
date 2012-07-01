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

/*
 * This file contains common utilities that are useful for pjmedia components
 * that use ffmpeg. This is not a public API.
 */

#ifndef __PJMEDIA_FFMPEG_UTIL_H__
#define __PJMEDIA_FFMPEG_UTIL_H__

#include <pjmedia/format.h>

#ifdef _MSC_VER
#   ifndef __cplusplus
#	define inline _inline
#   endif
#   pragma warning(disable:4244) /* possible loss of data */
#endif

#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>

void pjmedia_ffmpeg_add_ref();
void pjmedia_ffmpeg_dec_ref();

pj_status_t pjmedia_format_id_to_PixelFormat(pjmedia_format_id fmt_id,
					     enum PixelFormat *pixel_format);

pj_status_t PixelFormat_to_pjmedia_format_id(enum PixelFormat pf,
					     pjmedia_format_id *fmt_id);

pj_status_t pjmedia_format_id_to_CodecID(pjmedia_format_id fmt_id,
					 enum CodecID *codec_id);

pj_status_t CodecID_to_pjmedia_format_id(enum CodecID codec_id,
					 pjmedia_format_id *fmt_id);

#endif /* __PJMEDIA_FFMPEG_UTIL_H__ */
