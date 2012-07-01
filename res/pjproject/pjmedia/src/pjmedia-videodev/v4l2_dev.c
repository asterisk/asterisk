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
#include <pjmedia-videodev/videodev_imp.h>
#include <pjmedia/errno.h>
#include <pj/assert.h>
#include <pj/errno.h>
#include <pj/file_access.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/rand.h>

#if PJMEDIA_VIDEO_DEV_HAS_V4L2

#include <linux/videodev2.h>
#include <libv4l2.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

#define THIS_FILE		"v4l2_dev.c"
#define DRIVER_NAME		"v4l2"
#define V4L2_MAX_DEVS		4
#define DEFAULT_WIDTH		640
#define DEFAULT_HEIGHT		480
#define DEFAULT_FPS		25
#define DEFAULT_CLOCK_RATE	90000
#define INVALID_FD		-1
#define BUFFER_CNT		2
#define MAX_IOCTL_RETRY		20


/* mapping between pjmedia_fmt_id and v4l2 pixel format */
typedef struct vid4lin_fmt_map
{
    pj_uint32_t pjmedia_fmt_id;
    pj_uint32_t	v4l2_fmt_id;
} vid4lin_fmt_map;

/* I/O type being used */
enum vid4lin_io_type
{
    IO_TYPE_NONE,
    IO_TYPE_READ,
    IO_TYPE_MMAP,
    IO_TYPE_MMAP_USER
};

/* descriptor for each mmap-ed buffer */
typedef struct vid4lin_buffer
{
    void   *start;
    size_t  length;
} vid4lin_buffer;

/* v4l2 device info */
typedef struct vid4lin_dev_info
{
    pjmedia_vid_dev_info	 info;
    char			 dev_name[32];
    struct v4l2_capability 	 v4l2_cap;
} vid4lin_dev_info;

/* v4l2 factory */
typedef struct vid4lin_factory
{
    pjmedia_vid_dev_factory	 base;
    pj_pool_t			*pool;
    pj_pool_t			*dev_pool;
    pj_pool_factory		*pf;

    unsigned			 dev_count;
    vid4lin_dev_info		*dev_info;
} vid4lin_factory;

/* Video stream. */
typedef struct vid4lin_stream
{
    pjmedia_vid_dev_stream	 base;		/**< Base stream	*/
    pjmedia_vid_dev_param	 param;		/**< Settings		*/
    pj_pool_t           	*pool;		/**< Memory pool.	*/

    int			 	 fd;		/**< Video fd.		*/
    char			 name[64];	/**< Name for log	*/
    enum vid4lin_io_type	 io_type;	/**< I/O method.	*/
    unsigned			 buf_cnt;	/**< MMap buf cnt.  	*/
    vid4lin_buffer		*buffers;	/**< MMap buffers.  	*/
    pj_time_val			 start_time;	/**< Time when started	*/

    pjmedia_vid_dev_cb       	 vid_cb;	/**< Stream callback  	*/
    void                	*user_data;	/**< Application data 	*/
} vid4lin_stream;

/* Use this to convert between pjmedia_format_id and V4L2 fourcc */
static vid4lin_fmt_map v4l2_fmt_maps[] =
{
    { PJMEDIA_FORMAT_RGB24,	V4L2_PIX_FMT_BGR24 },
    { PJMEDIA_FORMAT_RGBA,	V4L2_PIX_FMT_BGR32 },
    { PJMEDIA_FORMAT_RGB32,	V4L2_PIX_FMT_BGR32 },
    { PJMEDIA_FORMAT_AYUV,	V4L2_PIX_FMT_YUV32 },
    { PJMEDIA_FORMAT_YUY2,	V4L2_PIX_FMT_YUYV },
    { PJMEDIA_FORMAT_UYVY,	V4L2_PIX_FMT_UYVY }
};

/* Prototypes */
static pj_status_t vid4lin_factory_init(pjmedia_vid_dev_factory *f);
static pj_status_t vid4lin_factory_destroy(pjmedia_vid_dev_factory *f);
static pj_status_t vid4lin_factory_refresh(pjmedia_vid_dev_factory *f);
static unsigned    vid4lin_factory_get_dev_count(pjmedia_vid_dev_factory *f);
static pj_status_t vid4lin_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					        unsigned index,
					        pjmedia_vid_dev_info *info);
static pj_status_t vid4lin_factory_default_param(pj_pool_t *pool,
                                                 pjmedia_vid_dev_factory *f,
					         unsigned index,
					         pjmedia_vid_dev_param *param);
static pj_status_t vid4lin_factory_create_stream(pjmedia_vid_dev_factory *f,
						 pjmedia_vid_dev_param *prm,
					         const pjmedia_vid_dev_cb *cb,
					         void *user_data,
					         pjmedia_vid_dev_stream **p);

static pj_status_t vid4lin_stream_get_param(pjmedia_vid_dev_stream *strm,
					    pjmedia_vid_dev_param *param);
static pj_status_t vid4lin_stream_get_cap(pjmedia_vid_dev_stream *strm,
				          pjmedia_vid_dev_cap cap,
				          void *value);
static pj_status_t vid4lin_stream_set_cap(pjmedia_vid_dev_stream *strm,
				          pjmedia_vid_dev_cap cap,
				          const void *value);
static pj_status_t vid4lin_stream_get_frame(pjmedia_vid_dev_stream *strm,
                                            pjmedia_frame *frame);
static pj_status_t vid4lin_stream_start(pjmedia_vid_dev_stream *strm);
static pj_status_t vid4lin_stream_stop(pjmedia_vid_dev_stream *strm);
static pj_status_t vid4lin_stream_destroy(pjmedia_vid_dev_stream *strm);

/* Operations */
static pjmedia_vid_dev_factory_op factory_op =
{
    &vid4lin_factory_init,
    &vid4lin_factory_destroy,
    &vid4lin_factory_get_dev_count,
    &vid4lin_factory_get_dev_info,
    &vid4lin_factory_default_param,
    &vid4lin_factory_create_stream,
    &vid4lin_factory_refresh
};

static pjmedia_vid_dev_stream_op stream_op =
{
    &vid4lin_stream_get_param,
    &vid4lin_stream_get_cap,
    &vid4lin_stream_set_cap,
    &vid4lin_stream_start,
    &vid4lin_stream_get_frame,
    NULL,
    &vid4lin_stream_stop,
    &vid4lin_stream_destroy
};


/****************************************************************************
 * Factory operations
 */
/*
 * Factory creation function.
 */
pjmedia_vid_dev_factory* pjmedia_v4l2_factory(pj_pool_factory *pf)
{
    vid4lin_factory *f;
    pj_pool_t *pool;

    pool = pj_pool_create(pf, DRIVER_NAME, 512, 512, NULL);
    f = PJ_POOL_ZALLOC_T(pool, vid4lin_factory);
    f->pf = pf;
    f->pool = pool;
    f->base.op = &factory_op;

    return &f->base;
}

/* util: ioctl that tries harder. */
static pj_status_t xioctl(int fh, int request, void *arg)
{
    enum { RETRY = MAX_IOCTL_RETRY };
    int r, c=0;

    do {
	r = v4l2_ioctl(fh, request, arg);
    } while (r==-1 && c++<RETRY && ((errno==EINTR) || (errno==EAGAIN)));

    return (r == -1) ? pj_get_os_error() : PJ_SUCCESS;
}

/* Scan V4L2 devices */
static pj_status_t v4l2_scan_devs(vid4lin_factory *f)
{
    vid4lin_dev_info vdi[V4L2_MAX_DEVS];
    char dev_name[32];
    unsigned i, old_count;
    pj_status_t status;

    if (f->dev_pool) {
        pj_pool_release(f->dev_pool);
        f->dev_pool = NULL;
    }

    pj_bzero(vdi, sizeof(vdi));
    old_count = f->dev_count;
    f->dev_count = 0;
    f->dev_pool = pj_pool_create(f->pf, DRIVER_NAME, 500, 500, NULL);

    for (i=0; i<V4L2_MAX_DEVS && f->dev_count < V4L2_MAX_DEVS; ++i) {
	int fd;
	vid4lin_dev_info *pdi;
	pj_uint32_t fmt_cap[8];
	int j, fmt_cnt=0;

	pdi = &vdi[f->dev_count];

	snprintf(dev_name, sizeof(dev_name), "/dev/video%d", i);
	if (!pj_file_exists(dev_name))
	    continue;

	fd = v4l2_open(dev_name, O_RDWR, 0);
	if (fd == -1)
	    continue;

	status = xioctl(fd, VIDIOC_QUERYCAP, &pdi->v4l2_cap);
	if (status != PJ_SUCCESS) {
	    PJ_PERROR(4,(THIS_FILE, status, "Error querying %s", dev_name));
	    v4l2_close(fd);
	    continue;
	}

	if ((pdi->v4l2_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
	    v4l2_close(fd);
	    continue;
	}

	PJ_LOG(5,(THIS_FILE, "Found capture device %s", pdi->v4l2_cap.card));
	PJ_LOG(5,(THIS_FILE, "  Enumerating formats:"));
	for (j=0; fmt_cnt<PJ_ARRAY_SIZE(fmt_cap); ++j) {
	    struct v4l2_fmtdesc fdesc;
	    unsigned k;

	    pj_bzero(&fdesc, sizeof(fdesc));
	    fdesc.index = j;
	    fdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	    status = xioctl(fd, VIDIOC_ENUM_FMT, &fdesc);
	    if (status != PJ_SUCCESS)
		break;

	    for (k=0; k<PJ_ARRAY_SIZE(v4l2_fmt_maps); ++k) {
		if (v4l2_fmt_maps[k].v4l2_fmt_id == fdesc.pixelformat) {
		    fmt_cap[fmt_cnt++] = v4l2_fmt_maps[k].pjmedia_fmt_id;
		    PJ_LOG(5,(THIS_FILE, "   Supported: %s",
			      fdesc.description));
		    break;
		}
	    }
	    if (k==PJ_ARRAY_SIZE(v4l2_fmt_maps)) {
		PJ_LOG(5,(THIS_FILE, "   Unsupported: %s", fdesc.description));
	    }
	}

	v4l2_close(fd);

	if (fmt_cnt==0) {
	    PJ_LOG(5,(THIS_FILE, "    Found no common format"));
	    continue;
	}

	strncpy(pdi->dev_name, dev_name, sizeof(pdi->dev_name));
	pdi->dev_name[sizeof(pdi->dev_name)-1] = '\0';
	strncpy(pdi->info.name, (char*)pdi->v4l2_cap.card,
		sizeof(pdi->info.name));
	pdi->info.name[sizeof(pdi->info.name)-1] = '\0';
	strncpy(pdi->info.driver, DRIVER_NAME, sizeof(pdi->info.driver));
	pdi->info.driver[sizeof(pdi->info.driver)-1] = '\0';
	pdi->info.dir = PJMEDIA_DIR_CAPTURE;
	pdi->info.has_callback = PJ_FALSE;
	pdi->info.caps = PJMEDIA_VID_DEV_CAP_FORMAT;

	pdi->info.fmt_cnt = fmt_cnt;
	for (j=0; j<fmt_cnt; ++j) {
	    pjmedia_format_init_video(&pdi->info.fmt[j],
				      fmt_cap[j],
				      DEFAULT_WIDTH,
				      DEFAULT_HEIGHT,
				      DEFAULT_FPS, 1);
	}
	if (j < fmt_cnt)
	    continue;

	f->dev_count++;
    }

    if (f->dev_count == 0)
	return PJ_SUCCESS;

    if (f->dev_count > old_count || f->dev_info == NULL) {
	f->dev_info = (vid4lin_dev_info*)
		      pj_pool_calloc(f->dev_pool,
				     f->dev_count,
				     sizeof(vid4lin_dev_info));
    }
    pj_memcpy(f->dev_info, vdi, f->dev_count * sizeof(vid4lin_dev_info));

    return PJ_SUCCESS;
}


/* API: init factory */
static pj_status_t vid4lin_factory_init(pjmedia_vid_dev_factory *f)
{
    return vid4lin_factory_refresh(f);
}

/* API: destroy factory */
static pj_status_t vid4lin_factory_destroy(pjmedia_vid_dev_factory *f)
{
    vid4lin_factory *cf = (vid4lin_factory*)f;
    pj_pool_t *pool = cf->pool;

    if (cf->dev_pool)
        pj_pool_release(cf->dev_pool);
    if (cf->pool) {
	cf->pool = NULL;
	pj_pool_release(pool);
    }

    return PJ_SUCCESS;
}

/* API: refresh the list of devices */
static pj_status_t vid4lin_factory_refresh(pjmedia_vid_dev_factory *f)
{
    vid4lin_factory *cf = (vid4lin_factory*)f;
    pj_status_t status;

    status = v4l2_scan_devs(cf);
    if (status != PJ_SUCCESS)
	return status;

    PJ_LOG(4, (THIS_FILE, "Video4Linux2 has %d devices",
	       cf->dev_count));

    return PJ_SUCCESS;
}

/* API: get number of devices */
static unsigned vid4lin_factory_get_dev_count(pjmedia_vid_dev_factory *f)
{
    vid4lin_factory *cf = (vid4lin_factory*)f;
    return cf->dev_count;
}

/* API: get device info */
static pj_status_t vid4lin_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					     unsigned index,
					     pjmedia_vid_dev_info *info)
{
    vid4lin_factory *cf = (vid4lin_factory*)f;

    PJ_ASSERT_RETURN(index < cf->dev_count, PJMEDIA_EVID_INVDEV);

    pj_memcpy(info, &cf->dev_info[index].info, sizeof(*info));

    return PJ_SUCCESS;
}

/* API: create default device parameter */
static pj_status_t vid4lin_factory_default_param(pj_pool_t *pool,
                                                 pjmedia_vid_dev_factory *f,
                                                 unsigned index,
                                                 pjmedia_vid_dev_param *param)
{
    vid4lin_factory *cf = (vid4lin_factory*)f;

    PJ_ASSERT_RETURN(index < cf->dev_count, PJMEDIA_EVID_INVDEV);

    pj_bzero(param, sizeof(*param));
    param->dir = PJMEDIA_DIR_CAPTURE;
    param->cap_id = index;
    param->rend_id = PJMEDIA_VID_INVALID_DEV;
    param->flags = PJMEDIA_VID_DEV_CAP_FORMAT;
    param->clock_rate = DEFAULT_CLOCK_RATE;
    pjmedia_format_copy(&param->fmt, &cf->dev_info[index].info.fmt[0]);

    return PJ_SUCCESS;
}

static vid4lin_fmt_map* get_v4l2_format_info(pjmedia_format_id id)
{
    unsigned i;

    for (i = 0; i < PJ_ARRAY_SIZE(v4l2_fmt_maps); i++) {
        if (v4l2_fmt_maps[i].pjmedia_fmt_id == id)
            return &v4l2_fmt_maps[i];
    }

    return NULL;
}

/* util: setup format */
static pj_status_t vid4lin_stream_init_fmt(vid4lin_stream *stream,
					const pjmedia_vid_dev_param *param,
					pj_uint32_t pix_fmt)
{
    pjmedia_video_format_detail *vfd;
    struct v4l2_format v4l2_fmt;
    pj_status_t status;

    vfd = pjmedia_format_get_video_format_detail(&param->fmt, PJ_TRUE);
    if (vfd == NULL)
	return PJMEDIA_EVID_BADFORMAT;

    pj_bzero(&v4l2_fmt, sizeof(v4l2_fmt));
    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_fmt.fmt.pix.width       = vfd->size.w;
    v4l2_fmt.fmt.pix.height      = vfd->size.h;
    v4l2_fmt.fmt.pix.pixelformat = pix_fmt;
    v4l2_fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
    status = xioctl(stream->fd, VIDIOC_S_FMT, &v4l2_fmt);
    if (status != PJ_SUCCESS)
	return status;

    if (v4l2_fmt.fmt.pix.pixelformat != pix_fmt) {
	status = PJMEDIA_EVID_BADFORMAT;
	return status;
    }

    if ((v4l2_fmt.fmt.pix.width != vfd->size.w) ||
	(v4l2_fmt.fmt.pix.height != vfd->size.h))
    {
	/* Size has changed */
	vfd->size.w = v4l2_fmt.fmt.pix.width;
	vfd->size.h = v4l2_fmt.fmt.pix.height;
    }

    return PJ_SUCCESS;
}

/* Util: initiate v4l2 streaming via mmap */
static pj_status_t vid4lin_stream_init_streaming(vid4lin_stream *stream)
{
    struct v4l2_requestbuffers req;
    unsigned i;
    pj_status_t status;

    pj_bzero(&req, sizeof(req));
    req.count = BUFFER_CNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    status = xioctl(stream->fd, VIDIOC_REQBUFS, &req);
    if (status != PJ_SUCCESS)
	return status;

    stream->buffers = pj_pool_calloc(stream->pool, req.count,
				     sizeof(*stream->buffers));
    stream->buf_cnt = 0;

    for (i = 0; i < req.count; ++i) {
	struct v4l2_buffer buf;

	pj_bzero(&buf, sizeof(buf));

	buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory      = V4L2_MEMORY_MMAP;
	buf.index       = i;

	status = xioctl(stream->fd, VIDIOC_QUERYBUF, &buf);
	if (status != PJ_SUCCESS)
	    goto on_error;

	stream->buffers[i].length = buf.length;
	stream->buffers[i].start = v4l2_mmap(NULL, buf.length,
					     PROT_READ | PROT_WRITE,
					     MAP_SHARED, stream->fd,
					     buf.m.offset);

	if (MAP_FAILED == stream->buffers[i].start) {
	    status = pj_get_os_error();
	    goto on_error;
	}

	stream->buf_cnt++;
    }

    PJ_LOG(5,(THIS_FILE, "  mmap streaming initialized"));

    stream->io_type = IO_TYPE_MMAP;
    return PJ_SUCCESS;

on_error:
    return status;
}

/* init streaming with user pointer */
static pj_status_t vid4lin_stream_init_streaming_user(vid4lin_stream *stream)
{
    return PJ_ENOTSUP;
}

/* init streaming with read() */
static pj_status_t vid4lin_stream_init_read_write(vid4lin_stream *stream)
{
    return PJ_ENOTSUP;
}

/* API: create stream */
static pj_status_t vid4lin_factory_create_stream(pjmedia_vid_dev_factory *f,
				      pjmedia_vid_dev_param *param,
				      const pjmedia_vid_dev_cb *cb,
				      void *user_data,
				      pjmedia_vid_dev_stream **p_vid_strm)
{
    vid4lin_factory *cf = (vid4lin_factory*)f;
    pj_pool_t *pool;
    vid4lin_stream *stream;
    vid4lin_dev_info *vdi;
    const vid4lin_fmt_map *fmt_map;
    const pjmedia_video_format_info *fmt_info;
    pjmedia_video_format_detail *vfd;
    pj_status_t status = PJ_SUCCESS;


    PJ_ASSERT_RETURN(f && param && p_vid_strm, PJ_EINVAL);
    PJ_ASSERT_RETURN(param->fmt.type == PJMEDIA_TYPE_VIDEO &&
		     param->fmt.detail_type == PJMEDIA_FORMAT_DETAIL_VIDEO &&
                     param->dir == PJMEDIA_DIR_CAPTURE,
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(param->cap_id >= 0 && param->cap_id < cf->dev_count,
		     PJMEDIA_EVID_INVDEV);

    fmt_info = pjmedia_get_video_format_info(NULL, param->fmt.id);
    if (!fmt_info || (fmt_map=get_v4l2_format_info(param->fmt.id))==NULL)
        return PJMEDIA_EVID_BADFORMAT;

    vdi = &cf->dev_info[param->cap_id];
    vfd = pjmedia_format_get_video_format_detail(&param->fmt, PJ_TRUE);

    /* Create and Initialize stream descriptor */
    pool = pj_pool_create(cf->pf, vdi->info.name, 512, 512, NULL);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    stream = PJ_POOL_ZALLOC_T(pool, vid4lin_stream);
    pj_memcpy(&stream->param, param, sizeof(*param));
    stream->pool = pool;
    pj_memcpy(&stream->vid_cb, cb, sizeof(*cb));
    strncpy(stream->name, vdi->info.name, sizeof(stream->name));
    stream->name[sizeof(stream->name)-1] = '\0';
    stream->user_data = user_data;
    stream->fd = INVALID_FD;

    stream->fd = v4l2_open(vdi->dev_name, O_RDWR, 0);
    if (stream->fd < 0)
	goto on_error;

    status = vid4lin_stream_init_fmt(stream, param, fmt_map->v4l2_fmt_id);
    if (status != PJ_SUCCESS)
	goto on_error;

    if (vdi->v4l2_cap.capabilities & V4L2_CAP_STREAMING)
	status = vid4lin_stream_init_streaming(stream);

    if (status!=PJ_SUCCESS && vdi->v4l2_cap.capabilities & V4L2_CAP_STREAMING)
	status = vid4lin_stream_init_streaming_user(stream);

    if (status!=PJ_SUCCESS && vdi->v4l2_cap.capabilities & V4L2_CAP_READWRITE)
	status = vid4lin_stream_init_read_write(stream);

    if (status != PJ_SUCCESS) {
	PJ_LOG(1,(THIS_FILE, "Error: unable to initiate I/O on %s",
		  stream->name));
	goto on_error;
    }

    /* Done */
    stream->base.op = &stream_op;
    *p_vid_strm = &stream->base;

    return PJ_SUCCESS;

on_error:
    if (status == PJ_SUCCESS)
	status = PJ_RETURN_OS_ERROR(errno);

    vid4lin_stream_destroy(&stream->base);
    return status;
}

/* API: Get stream info. */
static pj_status_t vid4lin_stream_get_param(pjmedia_vid_dev_stream *s,
					    pjmedia_vid_dev_param *pi)
{
    vid4lin_stream *strm = (vid4lin_stream*)s;

    PJ_ASSERT_RETURN(strm && pi, PJ_EINVAL);

    pj_memcpy(pi, &strm->param, sizeof(*pi));

    return PJ_SUCCESS;
}

/* API: get capability */
static pj_status_t vid4lin_stream_get_cap(pjmedia_vid_dev_stream *s,
                                          pjmedia_vid_dev_cap cap,
                                          void *pval)
{
    vid4lin_stream *strm = (vid4lin_stream*)s;

    PJ_UNUSED_ARG(strm);

    PJ_ASSERT_RETURN(s && pval, PJ_EINVAL);

    if (cap==PJMEDIA_VID_DEV_CAP_INPUT_SCALE)
    {
        return PJMEDIA_EVID_INVCAP;
//	return PJ_SUCCESS;
    } else {
	return PJMEDIA_EVID_INVCAP;
    }
}

/* API: set capability */
static pj_status_t vid4lin_stream_set_cap(pjmedia_vid_dev_stream *s,
                                          pjmedia_vid_dev_cap cap,
                                          const void *pval)
{
    vid4lin_stream *strm = (vid4lin_stream*)s;


    PJ_ASSERT_RETURN(s && pval, PJ_EINVAL);

    /*
    if (cap==PJMEDIA_VID_DEV_CAP_INPUT_SCALE)
    {
	return PJ_SUCCESS;
    }
    */
    PJ_UNUSED_ARG(strm);
    PJ_UNUSED_ARG(cap);
    PJ_UNUSED_ARG(pval);

    return PJMEDIA_EVID_INVCAP;
}

/* get frame from mmap */
static pj_status_t vid4lin_stream_get_frame_mmap(vid4lin_stream *stream,
                                                 pjmedia_frame *frame)
{
    struct v4l2_buffer buf;
    pj_time_val time;
    pj_status_t status = PJ_SUCCESS;

    pj_bzero(&buf, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    status = xioctl(stream->fd, VIDIOC_DQBUF, &buf);
    if (status != PJ_SUCCESS)
	return status;

    if (frame->size < buf.bytesused) {
	/* supplied buffer is too small */
	pj_assert(!"frame buffer is too small for v4l2");
	status = PJ_ETOOSMALL;
	goto on_return;
    }

    time.sec = buf.timestamp.tv_sec;
    time.msec = buf.timestamp.tv_usec / 1000;
    PJ_TIME_VAL_SUB(time, stream->start_time);

    frame->type = PJMEDIA_FRAME_TYPE_VIDEO;
    frame->bit_info = 0;
    frame->size = buf.bytesused;
    frame->timestamp.u64 = PJ_UINT64(1) * PJ_TIME_VAL_MSEC(time) *
			   stream->param.clock_rate / PJ_UINT64(1000);
    pj_memcpy(frame->buf, stream->buffers[buf.index].start, buf.bytesused);

on_return:
    pj_bzero(&buf, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    xioctl(stream->fd, VIDIOC_QBUF, &buf);

    return status;
}

/* API: Get frame from stream */
static pj_status_t vid4lin_stream_get_frame(pjmedia_vid_dev_stream *strm,
                                            pjmedia_frame *frame)
{
    vid4lin_stream *stream = (vid4lin_stream*)strm;

    if (stream->io_type == IO_TYPE_MMAP)
	return vid4lin_stream_get_frame_mmap(stream, frame);
    else {
	pj_assert(!"Unsupported i/o type");
	return PJ_EINVALIDOP;
    }
}

/* API: Start stream. */
static pj_status_t vid4lin_stream_start(pjmedia_vid_dev_stream *strm)
{
    vid4lin_stream *stream = (vid4lin_stream*)strm;
    struct v4l2_buffer buf;
    enum v4l2_buf_type type;
    unsigned i;
    pj_status_t status;

    PJ_ASSERT_RETURN(stream->fd != -1, PJ_EINVALIDOP);

    PJ_LOG(4, (THIS_FILE, "Starting v4l2 video stream %s", stream->name));

    pj_gettimeofday(&stream->start_time);

    for (i = 0; i < stream->buf_cnt; ++i) {
	pj_bzero(&buf, sizeof(buf));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = i;
	status = xioctl(stream->fd, VIDIOC_QBUF, &buf);
	if (status != PJ_SUCCESS)
	    goto on_error;
    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    status = xioctl(stream->fd, VIDIOC_STREAMON, &type);
    if (status != PJ_SUCCESS)
	goto on_error;

    return PJ_SUCCESS;

on_error:
    if (i > 0) {
	/* Dequeue already enqueued buffers. Can we do this while streaming
	 * is not started?
	 */
	unsigned n = i;
	for (i=0; i<n; ++i) {
	    pj_bzero(&buf, sizeof(buf));
	    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    buf.memory = V4L2_MEMORY_MMAP;
	    xioctl(stream->fd, VIDIOC_DQBUF, &buf);
	}
    }
    return status;
}

/* API: Stop stream. */
static pj_status_t vid4lin_stream_stop(pjmedia_vid_dev_stream *strm)
{
    vid4lin_stream *stream = (vid4lin_stream*)strm;
    enum v4l2_buf_type type;
    pj_status_t status;

    if (stream->fd < 0)
	return PJ_SUCCESS;

    PJ_LOG(4, (THIS_FILE, "Stopping v4l2 video stream %s", stream->name));

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    status = xioctl(stream->fd, VIDIOC_STREAMOFF, &type);
    if (status != PJ_SUCCESS)
	return status;

    return PJ_SUCCESS;
}


/* API: Destroy stream. */
static pj_status_t vid4lin_stream_destroy(pjmedia_vid_dev_stream *strm)
{
    vid4lin_stream *stream = (vid4lin_stream*)strm;
    unsigned i;

    PJ_ASSERT_RETURN(stream != NULL, PJ_EINVAL);

    vid4lin_stream_stop(strm);

    PJ_LOG(4, (THIS_FILE, "Destroying v4l2 video stream %s", stream->name));

    for (i=0; i<stream->buf_cnt; ++i) {
	if (stream->buffers[i].start != MAP_FAILED) {
	    v4l2_munmap(stream->buffers[i].start, stream->buffers[i].length);
	    stream->buffers[i].start = MAP_FAILED;
	}
    }

    if (stream->fd >= 0) {
	v4l2_close(stream->fd);
	stream->fd = -1;
    }
    pj_pool_release(stream->pool);

    return PJ_SUCCESS;
}

#endif	/* PJMEDIA_VIDEO_DEV_HAS_V4L2 */
