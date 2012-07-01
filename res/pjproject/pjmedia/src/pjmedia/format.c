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
#include <pjmedia/format.h>
#include <pj/assert.h>
#include <pj/errno.h>
#include <pj/pool.h>
#include <pj/string.h>


PJ_DEF(void) pjmedia_format_init_audio( pjmedia_format *fmt,
				        pj_uint32_t fmt_id,
					unsigned clock_rate,
					unsigned channel_count,
					unsigned bits_per_sample,
					unsigned frame_time_usec,
					pj_uint32_t avg_bps,
					pj_uint32_t max_bps)
{
    fmt->id = fmt_id;
    fmt->type = PJMEDIA_TYPE_AUDIO;
    fmt->detail_type = PJMEDIA_FORMAT_DETAIL_AUDIO;

    fmt->det.aud.clock_rate = clock_rate;
    fmt->det.aud.channel_count = channel_count;
    fmt->det.aud.bits_per_sample = bits_per_sample;
    fmt->det.aud.frame_time_usec = frame_time_usec;
    fmt->det.aud.avg_bps = avg_bps;
    fmt->det.aud.max_bps = max_bps;
}


PJ_DEF(pjmedia_audio_format_detail*)
pjmedia_format_get_audio_format_detail(const pjmedia_format *fmt,
				       pj_bool_t assert_valid)
{
    if (fmt->detail_type==PJMEDIA_FORMAT_DETAIL_AUDIO) {
	return (pjmedia_audio_format_detail*) &fmt->det.aud;
    } else {
        /* Get rid of unused var compiler warning if pj_assert()
         * macro does not do anything
         */
        PJ_UNUSED_ARG(assert_valid);
	pj_assert(!assert_valid || !"Invalid audio format detail");
	return NULL;
    }
}


PJ_DEF(pjmedia_format*) pjmedia_format_copy(pjmedia_format *dst,
					    const pjmedia_format *src)
{
    return (pjmedia_format*)pj_memcpy(dst, src, sizeof(*src));
}


#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)


static pj_status_t apply_packed_fmt(const pjmedia_video_format_info *fi,
	                            pjmedia_video_apply_fmt_param *aparam);

static pj_status_t apply_planar_420(const pjmedia_video_format_info *fi,
	                            pjmedia_video_apply_fmt_param *aparam);

static pj_status_t apply_planar_422(const pjmedia_video_format_info *fi,
	                            pjmedia_video_apply_fmt_param *aparam);

static pj_status_t apply_planar_444(const pjmedia_video_format_info *fi,
	                            pjmedia_video_apply_fmt_param *aparam);

struct pjmedia_video_format_mgr
{
    unsigned			max_info;
    unsigned			info_cnt;
    pjmedia_video_format_info **infos;
};

static pjmedia_video_format_mgr *video_format_mgr_instance;
static pjmedia_video_format_info built_in_vid_fmt_info[] =
{
    {PJMEDIA_FORMAT_RGB24, "RGB24", PJMEDIA_COLOR_MODEL_RGB, 24, 1, &apply_packed_fmt},
    {PJMEDIA_FORMAT_RGBA,  "RGBA", PJMEDIA_COLOR_MODEL_RGB, 32, 1, &apply_packed_fmt},
    {PJMEDIA_FORMAT_BGRA,  "BGRA", PJMEDIA_COLOR_MODEL_RGB, 32, 1, &apply_packed_fmt},
    {PJMEDIA_FORMAT_DIB ,  "DIB ", PJMEDIA_COLOR_MODEL_RGB, 24, 1, &apply_packed_fmt},
    {PJMEDIA_FORMAT_GBRP,  "GBRP", PJMEDIA_COLOR_MODEL_RGB, 24, 3, &apply_planar_444},
    {PJMEDIA_FORMAT_AYUV,  "AYUV", PJMEDIA_COLOR_MODEL_YUV, 32, 1, &apply_packed_fmt},
    {PJMEDIA_FORMAT_YUY2,  "YUY2", PJMEDIA_COLOR_MODEL_YUV, 16, 1, &apply_packed_fmt},
    {PJMEDIA_FORMAT_UYVY,  "UYVY", PJMEDIA_COLOR_MODEL_YUV, 16, 1, &apply_packed_fmt},
    {PJMEDIA_FORMAT_YVYU,  "YVYU", PJMEDIA_COLOR_MODEL_YUV, 16, 1, &apply_packed_fmt},
    {PJMEDIA_FORMAT_I420,  "I420", PJMEDIA_COLOR_MODEL_YUV, 12, 3, &apply_planar_420},
    {PJMEDIA_FORMAT_YV12,  "YV12", PJMEDIA_COLOR_MODEL_YUV, 16, 3, &apply_planar_422},
    {PJMEDIA_FORMAT_I420JPEG, "I420JPG", PJMEDIA_COLOR_MODEL_YUV, 12, 3, &apply_planar_420},
    {PJMEDIA_FORMAT_I422JPEG, "I422JPG", PJMEDIA_COLOR_MODEL_YUV, 16, 3, &apply_planar_422},
};

PJ_DEF(void) pjmedia_format_init_video( pjmedia_format *fmt,
					pj_uint32_t fmt_id,
					unsigned width,
					unsigned height,
					unsigned fps_num,
					unsigned fps_denum)
{
    pj_assert(fps_denum);
    fmt->id = fmt_id;
    fmt->type = PJMEDIA_TYPE_VIDEO;
    fmt->detail_type = PJMEDIA_FORMAT_DETAIL_VIDEO;

    fmt->det.vid.size.w = width;
    fmt->det.vid.size.h = height;
    fmt->det.vid.fps.num = fps_num;
    fmt->det.vid.fps.denum = fps_denum;
    fmt->det.vid.avg_bps = fmt->det.vid.max_bps = 0;

    if (pjmedia_video_format_mgr_instance()) {
	const pjmedia_video_format_info *vfi;
	pjmedia_video_apply_fmt_param vafp;
	pj_uint32_t bps;

	vfi = pjmedia_get_video_format_info(NULL, fmt->id);
        if (vfi) {
	    pj_bzero(&vafp, sizeof(vafp));
	    vafp.size = fmt->det.vid.size;
	    vfi->apply_fmt(vfi, &vafp);

	    bps = vafp.framebytes * fps_num * (pj_size_t)8 / fps_denum;
	    fmt->det.vid.avg_bps = fmt->det.vid.max_bps = bps;
        }
    }
}

PJ_DEF(pjmedia_video_format_detail*)
pjmedia_format_get_video_format_detail(const pjmedia_format *fmt,
				       pj_bool_t assert_valid)
{
    if (fmt->detail_type==PJMEDIA_FORMAT_DETAIL_VIDEO) {
	return (pjmedia_video_format_detail*)&fmt->det.vid;
    } else {
	pj_assert(!assert_valid || !"Invalid video format detail");
	return NULL;
    }
}


static pj_status_t apply_packed_fmt(const pjmedia_video_format_info *fi,
	                            pjmedia_video_apply_fmt_param *aparam)
{
    unsigned i;
    pj_size_t stride;

    stride = (pj_size_t)((aparam->size.w*fi->bpp) >> 3);

    /* Calculate memsize */
    aparam->framebytes = stride * aparam->size.h;

    /* Packed formats only use 1 plane */
    aparam->planes[0] = aparam->buffer;
    aparam->strides[0] = stride;
    aparam->plane_bytes[0] = aparam->framebytes;

    /* Zero unused planes */
    for (i=1; i<PJMEDIA_MAX_VIDEO_PLANES; ++i) {
	aparam->strides[i] = 0;
	aparam->planes[i] = NULL;
    }

    return PJ_SUCCESS;
}

static pj_status_t apply_planar_420(const pjmedia_video_format_info *fi,
	                             pjmedia_video_apply_fmt_param *aparam)
{
    unsigned i;
    pj_size_t Y_bytes;

    PJ_UNUSED_ARG(fi);

    /* Calculate memsize */
    Y_bytes = (pj_size_t)(aparam->size.w * aparam->size.h);
    aparam->framebytes = Y_bytes + (Y_bytes>>1);

    /* Planar formats use 3 plane */
    aparam->strides[0] = aparam->size.w;
    aparam->strides[1] = aparam->strides[2] = (aparam->size.w>>1);

    aparam->planes[0] = aparam->buffer;
    aparam->planes[1] = aparam->planes[0] + Y_bytes;
    aparam->planes[2] = aparam->planes[1] + (Y_bytes>>2);

    aparam->plane_bytes[0] = Y_bytes;
    aparam->plane_bytes[1] = aparam->plane_bytes[2] = (Y_bytes>>2);

    /* Zero unused planes */
    for (i=3; i<PJMEDIA_MAX_VIDEO_PLANES; ++i) {
	aparam->strides[i] = 0;
	aparam->planes[i] = NULL;
        aparam->plane_bytes[i] = 0;
    }

    return PJ_SUCCESS;
}

static pj_status_t apply_planar_422(const pjmedia_video_format_info *fi,
	                             pjmedia_video_apply_fmt_param *aparam)
{
    unsigned i;
    pj_size_t Y_bytes;

    PJ_UNUSED_ARG(fi);

    /* Calculate memsize */
    Y_bytes = (pj_size_t)(aparam->size.w * aparam->size.h);
    aparam->framebytes = (Y_bytes << 1);

    /* Planar formats use 3 plane */
    aparam->strides[0] = aparam->size.w;
    aparam->strides[1] = aparam->strides[2] = (aparam->size.w>>1);

    aparam->planes[0] = aparam->buffer;
    aparam->planes[1] = aparam->planes[0] + Y_bytes;
    aparam->planes[2] = aparam->planes[1] + (Y_bytes>>1);

    aparam->plane_bytes[0] = Y_bytes;
    aparam->plane_bytes[1] = aparam->plane_bytes[2] = (Y_bytes>>1);

    /* Zero unused planes */
    for (i=3; i<PJMEDIA_MAX_VIDEO_PLANES; ++i) {
	aparam->strides[i] = 0;
	aparam->planes[i] = NULL;
        aparam->plane_bytes[i] = 0;
    }

    return PJ_SUCCESS;
}

static pj_status_t apply_planar_444(const pjmedia_video_format_info *fi,
	                            pjmedia_video_apply_fmt_param *aparam)
{
    unsigned i;
    pj_size_t Y_bytes;

    PJ_UNUSED_ARG(fi);

    /* Calculate memsize */
    Y_bytes = (pj_size_t)(aparam->size.w * aparam->size.h);
    aparam->framebytes = (Y_bytes * 3);

    /* Planar formats use 3 plane */
    aparam->strides[0] = aparam->strides[1] = 
			 aparam->strides[2] = aparam->size.w;

    aparam->planes[0] = aparam->buffer;
    aparam->planes[1] = aparam->planes[0] + Y_bytes;
    aparam->planes[2] = aparam->planes[1] + Y_bytes;

    aparam->plane_bytes[0] = aparam->plane_bytes[1] =
			     aparam->plane_bytes[2] = Y_bytes;

    /* Zero unused planes */
    for (i=3; i<PJMEDIA_MAX_VIDEO_PLANES; ++i) {
	aparam->strides[i] = 0;
	aparam->planes[i] = NULL;
        aparam->plane_bytes[i] = 0;
    }

    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t)
pjmedia_video_format_mgr_create(pj_pool_t *pool,
				unsigned max_fmt,
				unsigned options,
				pjmedia_video_format_mgr **p_mgr)
{
    pjmedia_video_format_mgr *mgr;
    unsigned i;

    PJ_ASSERT_RETURN(pool && options==0, PJ_EINVAL);

    PJ_UNUSED_ARG(options);

    mgr = PJ_POOL_ALLOC_T(pool, pjmedia_video_format_mgr);
    mgr->max_info = max_fmt;
    mgr->info_cnt = 0;
    mgr->infos = pj_pool_calloc(pool, max_fmt, sizeof(pjmedia_video_format_info *));

    if (video_format_mgr_instance == NULL)
	video_format_mgr_instance = mgr;

    for (i=0; i<PJ_ARRAY_SIZE(built_in_vid_fmt_info); ++i) {
	pjmedia_register_video_format_info(mgr,
					   &built_in_vid_fmt_info[i]);
    }

    if (p_mgr)
	*p_mgr = mgr;

    return PJ_SUCCESS;
}


PJ_DEF(const pjmedia_video_format_info*)
pjmedia_get_video_format_info(pjmedia_video_format_mgr *mgr,
			      pj_uint32_t id)
{
    pjmedia_video_format_info **first;
    int		 comp;
    unsigned	 n;

    if (!mgr)
	mgr = pjmedia_video_format_mgr_instance();

    PJ_ASSERT_RETURN(mgr != NULL, NULL);

    /* Binary search for the appropriate format id */
    comp = -1;
    first = &mgr->infos[0];
    n = mgr->info_cnt;
    for (; n > 0; ) {
	unsigned half = n / 2;
	pjmedia_video_format_info **mid = first + half;

	if ((*mid)->id < id) {
	    first = ++mid;
	    n -= half + 1;
	} else if ((*mid)->id==id) {
	    return *mid;
	} else {
	    n = half;
	}
    }

    return NULL;
}


PJ_DEF(pj_status_t)
pjmedia_register_video_format_info(pjmedia_video_format_mgr *mgr,
				   pjmedia_video_format_info *info)
{
    unsigned i;

    if (!mgr)
	mgr = pjmedia_video_format_mgr_instance();

    PJ_ASSERT_RETURN(mgr != NULL, PJ_EINVALIDOP);

    if (mgr->info_cnt >= mgr->max_info)
	return PJ_ETOOMANY;

    /* Insert to the array, sorted */
    for (i=0; i<mgr->info_cnt; ++i) {
	if (mgr->infos[i]->id >= info->id)
	    break;
    }

    if (i < mgr->info_cnt) {
	if (mgr->infos[i]->id == info->id) {
	    /* just overwrite */
	    mgr->infos[i] = info;
	    return PJ_SUCCESS;
	}

	pj_memmove(&mgr->infos[i+1], &mgr->infos[i],
		   (mgr->info_cnt - i) * sizeof(pjmedia_video_format_info*));
    }

    mgr->infos[i] = info;
    mgr->info_cnt++;

    return PJ_SUCCESS;
}

PJ_DEF(pjmedia_video_format_mgr*) pjmedia_video_format_mgr_instance(void)
{
    pj_assert(video_format_mgr_instance != NULL);
    return video_format_mgr_instance;
}

PJ_DEF(void)
pjmedia_video_format_mgr_set_instance(pjmedia_video_format_mgr *mgr)
{
    video_format_mgr_instance = mgr;
}


PJ_DEF(void) pjmedia_video_format_mgr_destroy(pjmedia_video_format_mgr *mgr)
{
    if (!mgr)
	mgr = pjmedia_video_format_mgr_instance();

    PJ_ASSERT_ON_FAIL(mgr != NULL, return);

    mgr->info_cnt = 0;
    if (video_format_mgr_instance == mgr)
	video_format_mgr_instance = NULL;
}

#endif /* PJMEDIA_HAS_VIDEO */
