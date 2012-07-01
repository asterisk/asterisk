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

/* Video device with ffmpeg backend, currently only capture devices are
 * implemented.
 *
 * Issues:
 * - no device enumeration (ffmpeg limitation), so this uses "host API" enum
 *   instead
 * - need stricter filter on "host API" enum, currently audio capture devs are
 *   still listed.
 * - no format enumeration, currently hardcoded to PJMEDIA_FORMAT_RGB24 only
 * - tested on Vista only (vfw backend) with virtual cam
 * - vfw backend produce bottom up pictures
 * - using VS IDE, this cannot run under debugger!
 */

#include <pjmedia-videodev/videodev_imp.h>
#include <pj/assert.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/unicode.h>


#if defined(PJMEDIA_VIDEO_DEV_HAS_FFMPEG) && PJMEDIA_VIDEO_DEV_HAS_FFMPEG != 0


#define THIS_FILE		"ffmpeg.c"

#include "../pjmedia/ffmpeg_util.h"
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>

#define MAX_DEV_CNT     8

typedef struct ffmpeg_dev_info
{
    pjmedia_vid_dev_info         base;
    AVInputFormat               *host_api;
    const char                  *def_devname;
} ffmpeg_dev_info;


typedef struct ffmpeg_factory
{
    pjmedia_vid_dev_factory	 base;
    pj_pool_factory		*pf;
    pj_pool_t                   *pool;
    pj_pool_t                   *dev_pool;
    unsigned                     dev_count;
    ffmpeg_dev_info              dev_info[MAX_DEV_CNT];
} ffmpeg_factory;


typedef struct ffmpeg_stream
{
    pjmedia_vid_dev_stream       base;
    ffmpeg_factory              *factory;
    pj_pool_t                   *pool;
    pjmedia_vid_dev_param        param;
    AVFormatContext             *ff_fmt_ctx;
} ffmpeg_stream;


/* Prototypes */
static pj_status_t ffmpeg_factory_init(pjmedia_vid_dev_factory *f);
static pj_status_t ffmpeg_factory_destroy(pjmedia_vid_dev_factory *f);
static pj_status_t ffmpeg_factory_refresh(pjmedia_vid_dev_factory *f);
static unsigned    ffmpeg_factory_get_dev_count(pjmedia_vid_dev_factory *f);
static pj_status_t ffmpeg_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					       unsigned index,
					       pjmedia_vid_dev_info *info);
static pj_status_t ffmpeg_factory_default_param(pj_pool_t *pool,
                                                pjmedia_vid_dev_factory *f,
					        unsigned index,
					        pjmedia_vid_dev_param *param);
static pj_status_t ffmpeg_factory_create_stream(
					pjmedia_vid_dev_factory *f,
					pjmedia_vid_dev_param *param,
					const pjmedia_vid_dev_cb *cb,
					void *user_data,
					pjmedia_vid_dev_stream **p_vid_strm);

static pj_status_t ffmpeg_stream_get_param(pjmedia_vid_dev_stream *strm,
					   pjmedia_vid_dev_param *param);
static pj_status_t ffmpeg_stream_get_cap(pjmedia_vid_dev_stream *strm,
				         pjmedia_vid_dev_cap cap,
				         void *value);
static pj_status_t ffmpeg_stream_set_cap(pjmedia_vid_dev_stream *strm,
				         pjmedia_vid_dev_cap cap,
				         const void *value);
static pj_status_t ffmpeg_stream_start(pjmedia_vid_dev_stream *strm);
static pj_status_t ffmpeg_stream_get_frame(pjmedia_vid_dev_stream *s,
                                           pjmedia_frame *frame);
static pj_status_t ffmpeg_stream_stop(pjmedia_vid_dev_stream *strm);
static pj_status_t ffmpeg_stream_destroy(pjmedia_vid_dev_stream *strm);

/* Operations */
static pjmedia_vid_dev_factory_op factory_op =
{
    &ffmpeg_factory_init,
    &ffmpeg_factory_destroy,
    &ffmpeg_factory_get_dev_count,
    &ffmpeg_factory_get_dev_info,
    &ffmpeg_factory_default_param,
    &ffmpeg_factory_create_stream,
    &ffmpeg_factory_refresh
};

static pjmedia_vid_dev_stream_op stream_op =
{
    &ffmpeg_stream_get_param,
    &ffmpeg_stream_get_cap,
    &ffmpeg_stream_set_cap,
    &ffmpeg_stream_start,
    &ffmpeg_stream_get_frame,
    NULL,
    &ffmpeg_stream_stop,
    &ffmpeg_stream_destroy
};


static void print_ffmpeg_err(int err)
{
    char errbuf[512];
    if (av_strerror(err, errbuf, sizeof(errbuf)) >= 0)
        PJ_LOG(1, (THIS_FILE, "ffmpeg err %d: %s", err, errbuf));

}

static void print_ffmpeg_log(void* ptr, int level, const char* fmt, va_list vl)
{
    PJ_UNUSED_ARG(ptr);
    PJ_UNUSED_ARG(level);
    vfprintf(stdout, fmt, vl);
}


static pj_status_t ffmpeg_capture_open(AVFormatContext **ctx,
                                       AVInputFormat *ifmt,
                                       const char *dev_name,
                                       const pjmedia_vid_dev_param *param)
{
    AVFormatParameters fp;
    pjmedia_video_format_detail *vfd;
    int err;

    PJ_ASSERT_RETURN(ctx && ifmt && dev_name && param, PJ_EINVAL);
    PJ_ASSERT_RETURN(param->fmt.detail_type == PJMEDIA_FORMAT_DETAIL_VIDEO,
                     PJ_EINVAL);

    vfd = pjmedia_format_get_video_format_detail(&param->fmt, PJ_TRUE);

    /* Init ffmpeg format context */
    *ctx = avformat_alloc_context();

    /* Init ffmpeg format param */
    pj_bzero(&fp, sizeof(fp));
    fp.prealloced_context = 1;
    fp.width = vfd->size.w;
    fp.height = vfd->size.h;
    fp.pix_fmt = PIX_FMT_BGR24;
    fp.time_base.num = vfd->fps.denum;
    fp.time_base.den = vfd->fps.num;

    /* Open capture stream */
    err = av_open_input_stream(ctx, NULL, dev_name, ifmt, &fp);
    if (err < 0) {
        *ctx = NULL; /* ffmpeg freed its states on failure, do we must too */
        print_ffmpeg_err(err);
        return PJ_EUNKNOWN;
    }

    return PJ_SUCCESS;
}

static void ffmpeg_capture_close(AVFormatContext *ctx)
{
    if (ctx)
        av_close_input_stream(ctx);
}


/****************************************************************************
 * Factory operations
 */
/*
 * Init ffmpeg_ video driver.
 */
pjmedia_vid_dev_factory* pjmedia_ffmpeg_factory(pj_pool_factory *pf)
{
    ffmpeg_factory *f;
    pj_pool_t *pool;

    pool = pj_pool_create(pf, "ffmpeg_cap_dev", 1000, 1000, NULL);
    f = PJ_POOL_ZALLOC_T(pool, ffmpeg_factory);

    f->pool = pool;
    f->pf = pf;
    f->base.op = &factory_op;

    avdevice_register_all();

    return &f->base;
}


/* API: init factory */
static pj_status_t ffmpeg_factory_init(pjmedia_vid_dev_factory *f)
{
    return ffmpeg_factory_refresh(f);
}

/* API: destroy factory */
static pj_status_t ffmpeg_factory_destroy(pjmedia_vid_dev_factory *f)
{
    ffmpeg_factory *ff = (ffmpeg_factory*)f;
    pj_pool_t *pool = ff->pool;

    ff->dev_count = 0;
    ff->pool = NULL;
    if (ff->dev_pool)
        pj_pool_release(ff->dev_pool);
    if (pool)
        pj_pool_release(pool);

    return PJ_SUCCESS;
}

/* API: refresh the list of devices */
static pj_status_t ffmpeg_factory_refresh(pjmedia_vid_dev_factory *f)
{
    ffmpeg_factory *ff = (ffmpeg_factory*)f;
    AVInputFormat *p;
    ffmpeg_dev_info *info;

    av_log_set_callback(&print_ffmpeg_log);
    av_log_set_level(AV_LOG_DEBUG);

    if (ff->dev_pool) {
        pj_pool_release(ff->dev_pool);
        ff->dev_pool = NULL;
    }

    /* TODO: this should enumerate devices, now it enumerates host APIs */
    ff->dev_count = 0;
    ff->dev_pool = pj_pool_create(ff->pf, "ffmpeg_cap_dev", 500, 500, NULL);

    p = av_iformat_next(NULL);
    while (p) {
        if (p->flags & AVFMT_NOFILE) {
            unsigned i;

            info = &ff->dev_info[ff->dev_count++];
            pj_bzero(info, sizeof(*info));
            pj_ansi_strncpy(info->base.name, "default", 
                            sizeof(info->base.name));
            pj_ansi_snprintf(info->base.driver, sizeof(info->base.driver),
                             "%s (ffmpeg)", p->name);
            info->base.dir = PJMEDIA_DIR_CAPTURE;
            info->base.has_callback = PJ_FALSE;

            info->host_api = p;

#if defined(PJ_WIN32) && PJ_WIN32!=0
            info->def_devname = "0";
#elif defined(PJ_LINUX) && PJ_LINUX!=0
            info->def_devname = "/dev/video0";
#endif

            /* Set supported formats, currently hardcoded to RGB24 only */
            info->base.caps = PJMEDIA_VID_DEV_CAP_FORMAT;
            info->base.fmt_cnt = 1;
            for (i = 0; i < info->base.fmt_cnt; ++i) {
                pjmedia_format *fmt = &info->base.fmt[i];

                fmt->id = PJMEDIA_FORMAT_RGB24;
                fmt->type = PJMEDIA_TYPE_VIDEO;
                fmt->detail_type = PJMEDIA_FORMAT_DETAIL_NONE;
            }
        }
        p = av_iformat_next(p);
    }

    return PJ_SUCCESS;
}

/* API: get number of devices */
static unsigned ffmpeg_factory_get_dev_count(pjmedia_vid_dev_factory *f)
{
    ffmpeg_factory *ff = (ffmpeg_factory*)f;
    return ff->dev_count;
}

/* API: get device info */
static pj_status_t ffmpeg_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					       unsigned index,
					       pjmedia_vid_dev_info *info)
{
    ffmpeg_factory *ff = (ffmpeg_factory*)f;

    PJ_ASSERT_RETURN(index < ff->dev_count, PJMEDIA_EVID_INVDEV);

    pj_memcpy(info, &ff->dev_info[index].base, sizeof(*info));

    return PJ_SUCCESS;
}

/* API: create default device parameter */
static pj_status_t ffmpeg_factory_default_param(pj_pool_t *pool,
                                                pjmedia_vid_dev_factory *f,
					        unsigned index,
					        pjmedia_vid_dev_param *param)
{
    ffmpeg_factory *ff = (ffmpeg_factory*)f;
    ffmpeg_dev_info *info;

    PJ_ASSERT_RETURN(index < ff->dev_count, PJMEDIA_EVID_INVDEV);

    PJ_UNUSED_ARG(pool);

    info = &ff->dev_info[index];

    pj_bzero(param, sizeof(*param));
    param->dir = PJMEDIA_DIR_CAPTURE;
    param->cap_id = index;
    param->rend_id = PJMEDIA_VID_INVALID_DEV;
    param->clock_rate = 0;

    /* Set the device capabilities here */
    param->flags = PJMEDIA_VID_DEV_CAP_FORMAT;
    param->clock_rate = 90000;
    pjmedia_format_init_video(&param->fmt, 0, 320, 240, 25, 1);
    param->fmt.id = info->base.fmt[0].id;

    return PJ_SUCCESS;
}



/* API: create stream */
static pj_status_t ffmpeg_factory_create_stream(
					pjmedia_vid_dev_factory *f,
					pjmedia_vid_dev_param *param,
					const pjmedia_vid_dev_cb *cb,
					void *user_data,
					pjmedia_vid_dev_stream **p_vid_strm)
{
    ffmpeg_factory *ff = (ffmpeg_factory*)f;
    pj_pool_t *pool;
    ffmpeg_stream *strm;

    PJ_ASSERT_RETURN(f && param && p_vid_strm, PJ_EINVAL);
    PJ_ASSERT_RETURN(param->dir == PJMEDIA_DIR_CAPTURE, PJ_EINVAL);
    PJ_ASSERT_RETURN((unsigned)param->cap_id < ff->dev_count, PJ_EINVAL);
    PJ_ASSERT_RETURN(param->fmt.detail_type == PJMEDIA_FORMAT_DETAIL_VIDEO,
		     PJ_EINVAL);

    PJ_UNUSED_ARG(cb);
    PJ_UNUSED_ARG(user_data);

    /* Create and Initialize stream descriptor */
    pool = pj_pool_create(ff->pf, "ffmpeg-dev", 1000, 1000, NULL);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    strm = PJ_POOL_ZALLOC_T(pool, struct ffmpeg_stream);
    strm->factory = (ffmpeg_factory*)f;
    strm->pool = pool;
    pj_memcpy(&strm->param, param, sizeof(*param));

    /* Done */
    strm->base.op = &stream_op;
    *p_vid_strm = &strm->base;

    return PJ_SUCCESS;
}

/* API: Get stream info. */
static pj_status_t ffmpeg_stream_get_param(pjmedia_vid_dev_stream *s,
					   pjmedia_vid_dev_param *pi)
{
    ffmpeg_stream *strm = (ffmpeg_stream*)s;

    PJ_ASSERT_RETURN(strm && pi, PJ_EINVAL);

    pj_memcpy(pi, &strm->param, sizeof(*pi));

    return PJ_SUCCESS;
}

/* API: get capability */
static pj_status_t ffmpeg_stream_get_cap(pjmedia_vid_dev_stream *s,
				         pjmedia_vid_dev_cap cap,
				         void *pval)
{
    ffmpeg_stream *strm = (ffmpeg_stream*)s;

    PJ_UNUSED_ARG(strm);
    PJ_UNUSED_ARG(cap);
    PJ_UNUSED_ARG(pval);

    return PJMEDIA_EVID_INVCAP;
}

/* API: set capability */
static pj_status_t ffmpeg_stream_set_cap(pjmedia_vid_dev_stream *s,
				         pjmedia_vid_dev_cap cap,
				         const void *pval)
{
    ffmpeg_stream *strm = (ffmpeg_stream*)s;

    PJ_UNUSED_ARG(strm);
    PJ_UNUSED_ARG(cap);
    PJ_UNUSED_ARG(pval);

    return PJMEDIA_EVID_INVCAP;
}


/* API: Start stream. */
static pj_status_t ffmpeg_stream_start(pjmedia_vid_dev_stream *s)
{
    ffmpeg_stream *strm = (ffmpeg_stream*)s;
    ffmpeg_dev_info *info;
    pj_status_t status;

    info = &strm->factory->dev_info[strm->param.cap_id];

    PJ_LOG(4, (THIS_FILE, "Starting ffmpeg capture stream"));

    status = ffmpeg_capture_open(&strm->ff_fmt_ctx, info->host_api,
                                 info->def_devname, &strm->param);
    if (status != PJ_SUCCESS) {
        /* must set ffmpeg states to NULL on any failure */
        strm->ff_fmt_ctx = NULL;
    }

    return status;
}


/* API: Get frame from stream */
static pj_status_t ffmpeg_stream_get_frame(pjmedia_vid_dev_stream *s,
                                           pjmedia_frame *frame)
{
    ffmpeg_stream *strm = (ffmpeg_stream*)s;
    AVPacket p;
    int err;

    err = av_read_frame(strm->ff_fmt_ctx, &p);
    if (err < 0) {
        print_ffmpeg_err(err);
        return PJ_EUNKNOWN;
    }

    pj_bzero(frame, sizeof(*frame));
    frame->type = PJMEDIA_FRAME_TYPE_VIDEO;
    frame->buf = p.data;
    frame->size = p.size;

    return PJ_SUCCESS;
}


/* API: Stop stream. */
static pj_status_t ffmpeg_stream_stop(pjmedia_vid_dev_stream *s)
{
    ffmpeg_stream *strm = (ffmpeg_stream*)s;

    PJ_LOG(4, (THIS_FILE, "Stopping ffmpeg capture stream"));

    ffmpeg_capture_close(strm->ff_fmt_ctx);
    strm->ff_fmt_ctx = NULL;

    return PJ_SUCCESS;
}


/* API: Destroy stream. */
static pj_status_t ffmpeg_stream_destroy(pjmedia_vid_dev_stream *s)
{
    ffmpeg_stream *strm = (ffmpeg_stream*)s;

    PJ_ASSERT_RETURN(strm != NULL, PJ_EINVAL);

    ffmpeg_stream_stop(s);

    pj_pool_release(strm->pool);

    return PJ_SUCCESS;
}

#ifdef _MSC_VER
#   pragma comment( lib, "avdevice.lib")
#   pragma comment( lib, "avformat.lib")
#   pragma comment( lib, "avutil.lib")
#endif


#endif	/* PJMEDIA_VIDEO_DEV_HAS_FFMPEG */
