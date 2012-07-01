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
#include <pj/assert.h>
#include <pj/log.h>
#include <pj/os.h>

#if PJMEDIA_VIDEO_DEV_HAS_QT

#include <Foundation/NSAutoreleasePool.h>
#include <QTKit/QTKit.h>

#define THIS_FILE		"qt_dev.c"
#define DEFAULT_CLOCK_RATE	90000
#define DEFAULT_WIDTH		640
#define DEFAULT_HEIGHT		480
#define DEFAULT_FPS		15

#define kCVPixelFormatType_422YpCbCr8_yuvs 'yuvs'

typedef struct qt_fmt_info
{
    pjmedia_format_id   pjmedia_format;
    unsigned		qt_format;
} qt_fmt_info;

static qt_fmt_info qt_fmts[] =
{
    {PJMEDIA_FORMAT_YUY2, kCVPixelFormatType_422YpCbCr8_yuvs},
    {PJMEDIA_FORMAT_UYVY, kCVPixelFormatType_422YpCbCr8},
};

/* qt device info */
struct qt_dev_info
{
    pjmedia_vid_dev_info	 info;
    char			 dev_id[192];
};

/* qt factory */
struct qt_factory
{
    pjmedia_vid_dev_factory	 base;
    pj_pool_t			*pool;
    pj_pool_t			*dev_pool;
    pj_pool_factory		*pf;

    unsigned			 dev_count;
    struct qt_dev_info		*dev_info;
};

struct qt_stream;
typedef void (*func_ptr)(struct qt_stream *strm);

@interface QTDelegate: NSObject
{
@public
    struct qt_stream *strm;
    func_ptr          func;
}

- (void)run_func;
@end

/* Video stream. */
struct qt_stream
{
    pjmedia_vid_dev_stream  base;	    /**< Base stream	       */
    pjmedia_vid_dev_param   param;	    /**< Settings	       */
    pj_pool_t		   *pool;           /**< Memory pool.          */

    pj_timestamp	    cap_frame_ts;   /**< Captured frame tstamp */
    unsigned		    cap_ts_inc;	    /**< Increment	       */
    
    pjmedia_vid_dev_cb	    vid_cb;         /**< Stream callback.      */
    void		   *user_data;      /**< Application data.     */

    pj_bool_t		    cap_thread_exited;
    pj_bool_t		    cap_thread_initialized;
    pj_thread_desc	    cap_thread_desc;
    pj_thread_t		   *cap_thread;
    
    struct qt_factory      *qf;
    pj_status_t             status;
    pj_bool_t               is_running;
    pj_bool_t               cap_exited;
    
    QTCaptureSession			*cap_session;
    QTCaptureDeviceInput		*dev_input;
    QTCaptureDecompressedVideoOutput	*video_output;
    QTDelegate                          *qt_delegate;
};


/* Prototypes */
static pj_status_t qt_factory_init(pjmedia_vid_dev_factory *f);
static pj_status_t qt_factory_destroy(pjmedia_vid_dev_factory *f);
static pj_status_t qt_factory_refresh(pjmedia_vid_dev_factory *f);
static unsigned    qt_factory_get_dev_count(pjmedia_vid_dev_factory *f);
static pj_status_t qt_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					   unsigned index,
					   pjmedia_vid_dev_info *info);
static pj_status_t qt_factory_default_param(pj_pool_t *pool,
					    pjmedia_vid_dev_factory *f,
					    unsigned index,
					    pjmedia_vid_dev_param *param);
static pj_status_t qt_factory_create_stream(
					pjmedia_vid_dev_factory *f,
					pjmedia_vid_dev_param *param,
					const pjmedia_vid_dev_cb *cb,
					void *user_data,
					pjmedia_vid_dev_stream **p_vid_strm);

static pj_status_t qt_stream_get_param(pjmedia_vid_dev_stream *strm,
				       pjmedia_vid_dev_param *param);
static pj_status_t qt_stream_get_cap(pjmedia_vid_dev_stream *strm,
				     pjmedia_vid_dev_cap cap,
				     void *value);
static pj_status_t qt_stream_set_cap(pjmedia_vid_dev_stream *strm,
				     pjmedia_vid_dev_cap cap,
				     const void *value);
static pj_status_t qt_stream_start(pjmedia_vid_dev_stream *strm);
static pj_status_t qt_stream_stop(pjmedia_vid_dev_stream *strm);
static pj_status_t qt_stream_destroy(pjmedia_vid_dev_stream *strm);

/* Operations */
static pjmedia_vid_dev_factory_op factory_op =
{
    &qt_factory_init,
    &qt_factory_destroy,
    &qt_factory_get_dev_count,
    &qt_factory_get_dev_info,
    &qt_factory_default_param,
    &qt_factory_create_stream,
    &qt_factory_refresh
};

static pjmedia_vid_dev_stream_op stream_op =
{
    &qt_stream_get_param,
    &qt_stream_get_cap,
    &qt_stream_set_cap,
    &qt_stream_start,
    NULL,
    NULL,
    &qt_stream_stop,
    &qt_stream_destroy
};


/****************************************************************************
 * Factory operations
 */
/*
 * Init qt_ video driver.
 */
pjmedia_vid_dev_factory* pjmedia_qt_factory(pj_pool_factory *pf)
{
    struct qt_factory *f;
    pj_pool_t *pool;

    pool = pj_pool_create(pf, "qt video", 4000, 4000, NULL);
    f = PJ_POOL_ZALLOC_T(pool, struct qt_factory);
    f->pf = pf;
    f->pool = pool;
    f->base.op = &factory_op;

    return &f->base;
}


/* API: init factory */
static pj_status_t qt_factory_init(pjmedia_vid_dev_factory *f)
{
    return qt_factory_refresh(f);
}

/* API: destroy factory */
static pj_status_t qt_factory_destroy(pjmedia_vid_dev_factory *f)
{
    struct qt_factory *qf = (struct qt_factory*)f;
    pj_pool_t *pool = qf->pool;

    if (qf->dev_pool)
        pj_pool_release(qf->dev_pool);
    qf->pool = NULL;
    if (pool)
        pj_pool_release(pool);

    return PJ_SUCCESS;
}

/* API: refresh the list of devices */
static pj_status_t qt_factory_refresh(pjmedia_vid_dev_factory *f)
{
    struct qt_factory *qf = (struct qt_factory*)f;
    struct qt_dev_info *qdi;
    unsigned i, dev_count = 0;
    NSAutoreleasePool *apool = [[NSAutoreleasePool alloc]init];
    NSArray *dev_array;
    
    if (qf->dev_pool) {
        pj_pool_release(qf->dev_pool);
        qf->dev_pool = NULL;
    }
    
    dev_array = [QTCaptureDevice inputDevices];
    for (i = 0; i < [dev_array count]; i++) {
	QTCaptureDevice *dev = [dev_array objectAtIndex:i];
	if ([dev hasMediaType:QTMediaTypeVideo] ||
	    [dev hasMediaType:QTMediaTypeMuxed])
	{
	    dev_count++;
	}
    }
    
    /* Initialize input and output devices here */
    qf->dev_count = 0;
    qf->dev_pool = pj_pool_create(qf->pf, "qt video", 500, 500, NULL);
    
    qf->dev_info = (struct qt_dev_info*)
    pj_pool_calloc(qf->dev_pool, dev_count,
                   sizeof(struct qt_dev_info));
    for (i = 0; i < [dev_array count]; i++) {
	QTCaptureDevice *dev = [dev_array objectAtIndex:i];
	if ([dev hasMediaType:QTMediaTypeVideo] ||
	    [dev hasMediaType:QTMediaTypeMuxed])
	{
	    unsigned k;
	    
	    qdi = &qf->dev_info[qf->dev_count++];
	    pj_bzero(qdi, sizeof(*qdi));
	    [[dev localizedDisplayName] getCString:qdi->info.name
                                        maxLength:sizeof(qdi->info.name)
                                        encoding:
                                        [NSString defaultCStringEncoding]];
	    [[dev uniqueID] getCString:qdi->dev_id
                            maxLength:sizeof(qdi->dev_id)
                            encoding:[NSString defaultCStringEncoding]];
	    strcpy(qdi->info.driver, "QT");	    
	    qdi->info.dir = PJMEDIA_DIR_CAPTURE;
	    qdi->info.has_callback = PJ_TRUE;
            
	    qdi->info.fmt_cnt = 0;
	    qdi->info.caps = PJMEDIA_VID_DEV_CAP_FORMAT;
	    for (k = 0; k < [[dev formatDescriptions] count]; k++) {
		unsigned l;
		QTFormatDescription *desc = [[dev formatDescriptions]
					     objectAtIndex:k];
		for (l = 0; l < PJ_ARRAY_SIZE(qt_fmts); l++) {
		    if ([desc formatType] == qt_fmts[l].qt_format) {
			pjmedia_format *fmt = 
                            &qdi->info.fmt[qdi->info.fmt_cnt++];
			pjmedia_format_init_video(fmt,
						  qt_fmts[l].pjmedia_format,
						  DEFAULT_WIDTH,
						  DEFAULT_HEIGHT,
						  DEFAULT_FPS, 1);
			break;
		    }
		}
	    }
            
	    PJ_LOG(4, (THIS_FILE, " dev_id %d: %s", i, qdi->info.name));    
	}
    }
    
    [apool release];
    
    PJ_LOG(4, (THIS_FILE, "qt video has %d devices",
	       qf->dev_count));
    
    return PJ_SUCCESS;
}

/* API: get number of devices */
static unsigned qt_factory_get_dev_count(pjmedia_vid_dev_factory *f)
{
    struct qt_factory *qf = (struct qt_factory*)f;
    return qf->dev_count;
}

/* API: get device info */
static pj_status_t qt_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					   unsigned index,
					   pjmedia_vid_dev_info *info)
{
    struct qt_factory *qf = (struct qt_factory*)f;

    PJ_ASSERT_RETURN(index < qf->dev_count, PJMEDIA_EVID_INVDEV);

    pj_memcpy(info, &qf->dev_info[index].info, sizeof(*info));

    return PJ_SUCCESS;
}

/* API: create default device parameter */
static pj_status_t qt_factory_default_param(pj_pool_t *pool,
					    pjmedia_vid_dev_factory *f,
					    unsigned index,
					    pjmedia_vid_dev_param *param)
{
    struct qt_factory *qf = (struct qt_factory*)f;
    struct qt_dev_info *di = &qf->dev_info[index];

    PJ_ASSERT_RETURN(index < qf->dev_count, PJMEDIA_EVID_INVDEV);

    PJ_UNUSED_ARG(pool);

    pj_bzero(param, sizeof(*param));
    param->dir = PJMEDIA_DIR_CAPTURE;
    param->cap_id = index;
    param->rend_id = PJMEDIA_VID_INVALID_DEV;
    param->flags = PJMEDIA_VID_DEV_CAP_FORMAT;
    param->clock_rate = DEFAULT_CLOCK_RATE;
    pj_memcpy(&param->fmt, &di->info.fmt[0], sizeof(param->fmt));

    return PJ_SUCCESS;
}

static qt_fmt_info* get_qt_format_info(pjmedia_format_id id)
{
    unsigned i;
    
    for (i = 0; i < PJ_ARRAY_SIZE(qt_fmts); i++) {
        if (qt_fmts[i].pjmedia_format == id)
            return &qt_fmts[i];
    }
    
    return NULL;
}

@implementation QTDelegate
- (void)captureOutput:(QTCaptureOutput *)captureOutput
		      didOutputVideoFrame:(CVImageBufferRef)videoFrame
		      withSampleBuffer:(QTSampleBuffer *)sampleBuffer
		      fromConnection:(QTCaptureConnection *)connection
{
    unsigned size = [sampleBuffer lengthForAllSamples];
    pjmedia_frame frame;

    if (!strm->is_running) {
        strm->cap_exited = PJ_TRUE;
        return;
    }
    
    if (strm->cap_thread_initialized == 0 || !pj_thread_is_registered())
    {
	pj_thread_register("qt_cap", strm->cap_thread_desc,
			   &strm->cap_thread);
	strm->cap_thread_initialized = 1;
	PJ_LOG(5,(THIS_FILE, "Capture thread started"));
    }
    
    if (!videoFrame)
	return;
    
    frame.type = PJMEDIA_FRAME_TYPE_VIDEO;
    frame.buf = [sampleBuffer bytesForAllSamples];
    frame.size = size;
    frame.bit_info = 0;
    frame.timestamp.u64 = strm->cap_frame_ts.u64;
    
    if (strm->vid_cb.capture_cb)
        (*strm->vid_cb.capture_cb)(&strm->base, strm->user_data, &frame);
    
    strm->cap_frame_ts.u64 += strm->cap_ts_inc;
}

- (void)run_func
{
    (*func)(strm);
}

@end

static void init_qt(struct qt_stream *strm)
{
    const pjmedia_video_format_detail *vfd;
    qt_fmt_info *qfi = get_qt_format_info(strm->param.fmt.id);
    BOOL success = NO;
    NSError *error;
    
    if (!qfi) {
        strm->status = PJMEDIA_EVID_BADFORMAT;
        return;
    }
    
    strm->cap_session = [[QTCaptureSession alloc] init];
    if (!strm->cap_session) {
        strm->status = PJ_ENOMEM;
        return;
    }
    
    /* Open video device */
    QTCaptureDevice *videoDevice = 
        [QTCaptureDevice deviceWithUniqueID:
                         [NSString stringWithCString:
                                   strm->qf->dev_info[strm->param.cap_id].dev_id
                                   encoding:
                                   [NSString defaultCStringEncoding]]];
    if (!videoDevice || ![videoDevice open:&error]) {
        strm->status = PJMEDIA_EVID_SYSERR;
        return;
    }
    
    /* Add the video device to the session as a device input */	
    strm->dev_input = [[QTCaptureDeviceInput alloc] 
                       initWithDevice:videoDevice];
    success = [strm->cap_session addInput:strm->dev_input error:&error];
    if (!success) {
        strm->status = PJMEDIA_EVID_SYSERR;
        return;
    }
    
    strm->video_output = [[QTCaptureDecompressedVideoOutput alloc] init];
    success = [strm->cap_session addOutput:strm->video_output
                                 error:&error];
    if (!success) {
        strm->status = PJMEDIA_EVID_SYSERR;
        return;
    }
    
    vfd = pjmedia_format_get_video_format_detail(&strm->param.fmt,
                                                 PJ_TRUE);
    [strm->video_output setPixelBufferAttributes:
                        [NSDictionary dictionaryWithObjectsAndKeys:
                                      [NSNumber numberWithInt:qfi->qt_format],
                                      kCVPixelBufferPixelFormatTypeKey,
                                      [NSNumber numberWithInt:vfd->size.w],
                                      kCVPixelBufferWidthKey,
                                      [NSNumber numberWithInt:vfd->size.h],
                                      kCVPixelBufferHeightKey, nil]];
    
    pj_assert(vfd->fps.num);
    strm->cap_ts_inc = PJMEDIA_SPF2(strm->param.clock_rate, &vfd->fps, 1);
    
    if ([strm->video_output
         respondsToSelector:@selector(setMinimumVideoFrameInterval)])
    {
        [strm->video_output setMinimumVideoFrameInterval:
                            (1.0f * vfd->fps.denum / (double)vfd->fps.num)];
    }
    
    strm->qt_delegate = [[QTDelegate alloc]init];
    strm->qt_delegate->strm = strm;
    [strm->video_output setDelegate:strm->qt_delegate];
}    

static void run_func_on_main_thread(struct qt_stream *strm, func_ptr func)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    QTDelegate *delg = [[QTDelegate alloc] init];
    
    delg->strm = strm;
    delg->func = func;
    [delg performSelectorOnMainThread:@selector(run_func)
                           withObject:nil waitUntilDone:YES];
    
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);

    [delg release];
    [pool release];    
}

/* API: create stream */
static pj_status_t qt_factory_create_stream(
					pjmedia_vid_dev_factory *f,
					pjmedia_vid_dev_param *param,
					const pjmedia_vid_dev_cb *cb,
					void *user_data,
					pjmedia_vid_dev_stream **p_vid_strm)
{
    struct qt_factory *qf = (struct qt_factory*)f;
    pj_pool_t *pool;
    struct qt_stream *strm;
    const pjmedia_video_format_info *vfi;
    pj_status_t status = PJ_SUCCESS;

    PJ_ASSERT_RETURN(f && param && p_vid_strm, PJ_EINVAL);
    PJ_ASSERT_RETURN(param->fmt.type == PJMEDIA_TYPE_VIDEO &&
		     param->fmt.detail_type == PJMEDIA_FORMAT_DETAIL_VIDEO &&
                     param->dir == PJMEDIA_DIR_CAPTURE,
		     PJ_EINVAL);

    vfi = pjmedia_get_video_format_info(NULL, param->fmt.id);
    if (!vfi)
        return PJMEDIA_EVID_BADFORMAT;

    /* Create and Initialize stream descriptor */
    pool = pj_pool_create(qf->pf, "qt-dev", 4000, 4000, NULL);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    strm = PJ_POOL_ZALLOC_T(pool, struct qt_stream);
    pj_memcpy(&strm->param, param, sizeof(*param));
    strm->pool = pool;
    pj_memcpy(&strm->vid_cb, cb, sizeof(*cb));
    strm->user_data = user_data;
    strm->qf = qf;

    /* Create capture stream here */
    if (param->dir & PJMEDIA_DIR_CAPTURE) {        
        strm->status = PJ_SUCCESS;
        run_func_on_main_thread(strm, init_qt);
        if ((status = strm->status) != PJ_SUCCESS)
            goto on_error;
    }
    
    /* Apply the remaining settings */
    /*    
     if (param->flags & PJMEDIA_VID_DEV_CAP_INPUT_SCALE) {
	qt_stream_set_cap(&strm->base,
			  PJMEDIA_VID_DEV_CAP_INPUT_SCALE,
			  &param->fmt);
     }
     */
    /* Done */
    strm->base.op = &stream_op;
    *p_vid_strm = &strm->base;
    
    return PJ_SUCCESS;
    
on_error:
    qt_stream_destroy((pjmedia_vid_dev_stream *)strm);
    
    return status;
}

/* API: Get stream info. */
static pj_status_t qt_stream_get_param(pjmedia_vid_dev_stream *s,
				       pjmedia_vid_dev_param *pi)
{
    struct qt_stream *strm = (struct qt_stream*)s;

    PJ_ASSERT_RETURN(strm && pi, PJ_EINVAL);

    pj_memcpy(pi, &strm->param, sizeof(*pi));

/*    if (qt_stream_get_cap(s, PJMEDIA_VID_DEV_CAP_INPUT_SCALE,
                            &pi->fmt.info_size) == PJ_SUCCESS)
    {
        pi->flags |= PJMEDIA_VID_DEV_CAP_INPUT_SCALE;
    }
*/
    return PJ_SUCCESS;
}

/* API: get capability */
static pj_status_t qt_stream_get_cap(pjmedia_vid_dev_stream *s,
				     pjmedia_vid_dev_cap cap,
				     void *pval)
{
    struct qt_stream *strm = (struct qt_stream*)s;

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
static pj_status_t qt_stream_set_cap(pjmedia_vid_dev_stream *s,
				     pjmedia_vid_dev_cap cap,
				     const void *pval)
{
    struct qt_stream *strm = (struct qt_stream*)s;

    PJ_UNUSED_ARG(strm);

    PJ_ASSERT_RETURN(s && pval, PJ_EINVAL);

    if (cap==PJMEDIA_VID_DEV_CAP_INPUT_SCALE)
    {
	return PJ_SUCCESS;
    }

    return PJMEDIA_EVID_INVCAP;
}

static void start_qt(struct qt_stream *strm)
{
    [strm->cap_session startRunning];
}

static void stop_qt(struct qt_stream *strm)
{
    [strm->cap_session stopRunning];
}

/* API: Start stream. */
static pj_status_t qt_stream_start(pjmedia_vid_dev_stream *strm)
{
    struct qt_stream *stream = (struct qt_stream*)strm;

    PJ_UNUSED_ARG(stream);

    PJ_LOG(4, (THIS_FILE, "Starting qt video stream"));

    if (stream->cap_session) {
        run_func_on_main_thread(stream, start_qt);
    
	if (![stream->cap_session isRunning])
	    return PJMEDIA_EVID_NOTREADY;
        
        stream->is_running = PJ_TRUE;
    }

    return PJ_SUCCESS;
}

/* API: Stop stream. */
static pj_status_t qt_stream_stop(pjmedia_vid_dev_stream *strm)
{
    struct qt_stream *stream = (struct qt_stream*)strm;

    PJ_UNUSED_ARG(stream);

    PJ_LOG(4, (THIS_FILE, "Stopping qt video stream"));

    if (stream->cap_session && [stream->cap_session isRunning]) {
        int i;
        
        stream->cap_exited = PJ_FALSE;
        run_func_on_main_thread(stream, stop_qt);
        
        stream->is_running = PJ_FALSE;
        for (i = 50; i >= 0 && !stream->cap_exited; i--) {
            pj_thread_sleep(10);
        }
    }
    
    return PJ_SUCCESS;
}

static void destroy_qt(struct qt_stream *strm)
{
    if (strm->dev_input && [[strm->dev_input device] isOpen])
	[[strm->dev_input device] close];
    
    if (strm->cap_session) {
	[strm->cap_session release];
	strm->cap_session = NULL;
    }
    if (strm->dev_input) {
	[strm->dev_input release];
	strm->dev_input = NULL;
    }
    if (strm->qt_delegate) {
	[strm->qt_delegate release];
	strm->qt_delegate = NULL;
    }
    if (strm->video_output) {
	[strm->video_output release];
	strm->video_output = NULL;
    }
}

/* API: Destroy stream. */
static pj_status_t qt_stream_destroy(pjmedia_vid_dev_stream *strm)
{
    struct qt_stream *stream = (struct qt_stream*)strm;

    PJ_ASSERT_RETURN(stream != NULL, PJ_EINVAL);

    qt_stream_stop(strm);

    run_func_on_main_thread(stream, destroy_qt);
    
    pj_pool_release(stream->pool);

    return PJ_SUCCESS;
}

#endif	/* PJMEDIA_VIDEO_DEV_HAS_QT */
