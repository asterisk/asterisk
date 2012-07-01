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
#include <pjmedia/converter.h>
#include <pj/errno.h>

#if PJMEDIA_HAS_LIBSWSCALE && PJMEDIA_HAS_LIBAVUTIL

#include "ffmpeg_util.h"
#include <libswscale/swscale.h>

static pj_status_t factory_create_converter(pjmedia_converter_factory *cf,
					    pj_pool_t *pool,
					    const pjmedia_conversion_param*prm,
					    pjmedia_converter **p_cv);
static void factory_destroy_factory(pjmedia_converter_factory *cf);
static pj_status_t libswscale_conv_convert(pjmedia_converter *converter,
					   pjmedia_frame *src_frame,
					   pjmedia_frame *dst_frame);
static void libswscale_conv_destroy(pjmedia_converter *converter);


struct fmt_info
{
    const pjmedia_video_format_info 	*fmt_info;
    pjmedia_video_apply_fmt_param 	 apply_param;
};

struct ffmpeg_converter
{
    pjmedia_converter 			 base;
    struct SwsContext 			*sws_ctx;
    struct fmt_info			 src,
					 dst;
};

static pjmedia_converter_factory_op libswscale_factory_op =
{
    &factory_create_converter,
    &factory_destroy_factory
};

static pjmedia_converter_op liswscale_converter_op =
{
    &libswscale_conv_convert,
    &libswscale_conv_destroy
};

static pj_status_t factory_create_converter(pjmedia_converter_factory *cf,
					    pj_pool_t *pool,
					    const pjmedia_conversion_param *prm,
					    pjmedia_converter **p_cv)
{
    enum PixelFormat srcFormat, dstFormat;
    const pjmedia_video_format_detail *src_detail, *dst_detail;
    const pjmedia_video_format_info *src_fmt_info, *dst_fmt_info;
    struct SwsContext *sws_ctx;
    struct ffmpeg_converter *fcv;
    pj_status_t status;

    PJ_UNUSED_ARG(cf);

    /* Only supports video */
    if (prm->src.type != PJMEDIA_TYPE_VIDEO ||
	prm->dst.type != prm->src.type ||
	prm->src.detail_type != PJMEDIA_FORMAT_DETAIL_VIDEO ||
	prm->dst.detail_type != prm->src.detail_type)
    {
	return PJ_ENOTSUP;
    }

    /* lookup source format info */
    src_fmt_info = pjmedia_get_video_format_info(
		      pjmedia_video_format_mgr_instance(),
		      prm->src.id);
    if (!src_fmt_info)
	return PJ_ENOTSUP;

    /* lookup destination format info */
    dst_fmt_info = pjmedia_get_video_format_info(
		      pjmedia_video_format_mgr_instance(),
		      prm->dst.id);
    if (!dst_fmt_info)
	return PJ_ENOTSUP;

    src_detail = pjmedia_format_get_video_format_detail(&prm->src, PJ_TRUE);
    dst_detail = pjmedia_format_get_video_format_detail(&prm->dst, PJ_TRUE);

    status = pjmedia_format_id_to_PixelFormat(prm->src.id, &srcFormat);
    if (status != PJ_SUCCESS)
	return PJ_ENOTSUP;

    status = pjmedia_format_id_to_PixelFormat(prm->dst.id, &dstFormat);
    if (status != PJ_SUCCESS)
	return PJ_ENOTSUP;

    sws_ctx = sws_getContext(src_detail->size.w, src_detail->size.h, srcFormat,
		             dst_detail->size.w, dst_detail->size.h, dstFormat,
			     SWS_BICUBIC,
			     NULL, NULL, NULL);
    if (sws_ctx == NULL)
	return PJ_ENOTSUP;

    fcv = PJ_POOL_ZALLOC_T(pool, struct ffmpeg_converter);
    fcv->base.op = &liswscale_converter_op;
    fcv->sws_ctx = sws_ctx;
    fcv->src.apply_param.size = src_detail->size;
    fcv->src.fmt_info = src_fmt_info;
    fcv->dst.apply_param.size = dst_detail->size;
    fcv->dst.fmt_info = dst_fmt_info;

    *p_cv = &fcv->base;

    return PJ_SUCCESS;
}

static void factory_destroy_factory(pjmedia_converter_factory *cf)
{
    PJ_UNUSED_ARG(cf);
}

static pj_status_t libswscale_conv_convert(pjmedia_converter *converter,
					   pjmedia_frame *src_frame,
					   pjmedia_frame *dst_frame)
{
    struct ffmpeg_converter *fcv = (struct ffmpeg_converter*)converter;
    struct fmt_info *src = &fcv->src,
	            *dst = &fcv->dst;
    int h;

    src->apply_param.buffer = src_frame->buf;
    (*src->fmt_info->apply_fmt)(src->fmt_info, &src->apply_param);

    dst->apply_param.buffer = dst_frame->buf;
    (*dst->fmt_info->apply_fmt)(dst->fmt_info, &dst->apply_param);

    h = sws_scale(fcv->sws_ctx,
	          (const uint8_t* const *)src->apply_param.planes,
	          src->apply_param.strides,
		  0, src->apply_param.size.h,
		  dst->apply_param.planes, dst->apply_param.strides);

    //sws_scale() return value can't be trusted? There are cases when
    //sws_scale() returns zero but conversion seems to work okay.
    //return h==(int)dst->apply_param.size.h ? PJ_SUCCESS : PJ_EUNKNOWN;
    PJ_UNUSED_ARG(h);

    return PJ_SUCCESS;
}

static void libswscale_conv_destroy(pjmedia_converter *converter)
{
    struct ffmpeg_converter *fcv = (struct ffmpeg_converter*)converter;
    if (fcv->sws_ctx) {
	struct SwsContext *tmp = fcv->sws_ctx;
	fcv->sws_ctx = NULL;
	sws_freeContext(tmp);
    }
}

static pjmedia_converter_factory libswscale_factory =
{
    NULL, NULL,					/* list */
    "libswscale",				/* name */
    PJMEDIA_CONVERTER_PRIORITY_NORMAL+1,	/* priority */
    NULL					/* op will be init-ed later  */
};

PJ_DEF(pj_status_t)
pjmedia_libswscale_converter_init(pjmedia_converter_mgr *mgr)
{
    libswscale_factory.op = &libswscale_factory_op;
    pjmedia_ffmpeg_add_ref();
    return pjmedia_converter_mgr_register_factory(mgr, &libswscale_factory);
}


PJ_DEF(pj_status_t)
pjmedia_libswscale_converter_shutdown(pjmedia_converter_mgr *mgr,
				      pj_pool_t *pool)
{
    PJ_UNUSED_ARG(pool);
    pjmedia_ffmpeg_dec_ref();
    return pjmedia_converter_mgr_unregister_factory(mgr, &libswscale_factory,
						    PJ_TRUE);
}

#ifdef _MSC_VER
#   pragma comment( lib, "avutil.lib")
#   pragma comment( lib, "swscale.lib")
#endif

#endif /* #if PJMEDIA_HAS_LIBSWSCALE && PJMEDIA_HAS_LIBAVUTIL */
