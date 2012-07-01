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

#if PJMEDIA_VIDEO_DEV_HAS_IOS
#include "Availability.h"
#ifdef __IPHONE_4_0

#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>

#define THIS_FILE		"ios_dev.c"
#define DEFAULT_CLOCK_RATE	90000
#define DEFAULT_WIDTH		480
#define DEFAULT_HEIGHT		360
#define DEFAULT_FPS		15

typedef struct ios_fmt_info
{
    pjmedia_format_id   pjmedia_format;
    UInt32		ios_format;
} ios_fmt_info;

static ios_fmt_info ios_fmts[] =
{
    {PJMEDIA_FORMAT_BGRA, kCVPixelFormatType_32BGRA} ,
};

/* qt device info */
struct ios_dev_info
{
    pjmedia_vid_dev_info	 info;
};

/* qt factory */
struct ios_factory
{
    pjmedia_vid_dev_factory	 base;
    pj_pool_t			*pool;
    pj_pool_factory		*pf;

    unsigned			 dev_count;
    struct ios_dev_info		*dev_info;
};

@interface VOutDelegate: NSObject 
			 <AVCaptureVideoDataOutputSampleBufferDelegate>
{
@public
    struct ios_stream *stream;
}
@end

/* Video stream. */
struct ios_stream
{
    pjmedia_vid_dev_stream  base;		/**< Base stream       */
    pjmedia_vid_dev_param   param;		/**< Settings	       */
    pj_pool_t		   *pool;		/**< Memory pool       */

    pjmedia_vid_dev_cb	    vid_cb;		/**< Stream callback   */
    void		   *user_data;          /**< Application data  */

    pjmedia_rect_size	    size;
    pj_uint8_t		    bpp;
    unsigned		    bytes_per_row;
    unsigned		    frame_size;
    
    AVCaptureSession		*cap_session;
    AVCaptureDeviceInput	*dev_input;
    AVCaptureVideoDataOutput	*video_output;
    VOutDelegate		*vout_delegate;
    
    UIImageView		*imgView;
    void		*buf;
    dispatch_queue_t     render_queue;
    
    pj_timestamp	 frame_ts;
    unsigned		 ts_inc;
};


/* Prototypes */
static pj_status_t ios_factory_init(pjmedia_vid_dev_factory *f);
static pj_status_t ios_factory_destroy(pjmedia_vid_dev_factory *f);
static pj_status_t ios_factory_refresh(pjmedia_vid_dev_factory *f);
static unsigned    ios_factory_get_dev_count(pjmedia_vid_dev_factory *f);
static pj_status_t ios_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					    unsigned index,
					    pjmedia_vid_dev_info *info);
static pj_status_t ios_factory_default_param(pj_pool_t *pool,
					     pjmedia_vid_dev_factory *f,
					     unsigned index,
					     pjmedia_vid_dev_param *param);
static pj_status_t ios_factory_create_stream(
					pjmedia_vid_dev_factory *f,
					pjmedia_vid_dev_param *param,
					const pjmedia_vid_dev_cb *cb,
					void *user_data,
					pjmedia_vid_dev_stream **p_vid_strm);

static pj_status_t ios_stream_get_param(pjmedia_vid_dev_stream *strm,
				        pjmedia_vid_dev_param *param);
static pj_status_t ios_stream_get_cap(pjmedia_vid_dev_stream *strm,
				      pjmedia_vid_dev_cap cap,
				      void *value);
static pj_status_t ios_stream_set_cap(pjmedia_vid_dev_stream *strm,
				      pjmedia_vid_dev_cap cap,
				      const void *value);
static pj_status_t ios_stream_start(pjmedia_vid_dev_stream *strm);
static pj_status_t ios_stream_put_frame(pjmedia_vid_dev_stream *strm,
					const pjmedia_frame *frame);
static pj_status_t ios_stream_stop(pjmedia_vid_dev_stream *strm);
static pj_status_t ios_stream_destroy(pjmedia_vid_dev_stream *strm);

/* Operations */
static pjmedia_vid_dev_factory_op factory_op =
{
    &ios_factory_init,
    &ios_factory_destroy,
    &ios_factory_get_dev_count,
    &ios_factory_get_dev_info,
    &ios_factory_default_param,
    &ios_factory_create_stream,
    &ios_factory_refresh
};

static pjmedia_vid_dev_stream_op stream_op =
{
    &ios_stream_get_param,
    &ios_stream_get_cap,
    &ios_stream_set_cap,
    &ios_stream_start,
    NULL,
    &ios_stream_put_frame,
    &ios_stream_stop,
    &ios_stream_destroy
};


/****************************************************************************
 * Factory operations
 */
/*
 * Init ios_ video driver.
 */
pjmedia_vid_dev_factory* pjmedia_ios_factory(pj_pool_factory *pf)
{
    struct ios_factory *f;
    pj_pool_t *pool;

    pool = pj_pool_create(pf, "ios video", 512, 512, NULL);
    f = PJ_POOL_ZALLOC_T(pool, struct ios_factory);
    f->pf = pf;
    f->pool = pool;
    f->base.op = &factory_op;

    return &f->base;
}


/* API: init factory */
static pj_status_t ios_factory_init(pjmedia_vid_dev_factory *f)
{
    struct ios_factory *qf = (struct ios_factory*)f;
    struct ios_dev_info *qdi;
    unsigned i, l;
    
    /* Initialize input and output devices here */
    qf->dev_info = (struct ios_dev_info*)
		   pj_pool_calloc(qf->pool, 2,
				  sizeof(struct ios_dev_info));
    
    qf->dev_count = 0;
    qdi = &qf->dev_info[qf->dev_count++];
    pj_bzero(qdi, sizeof(*qdi));
    strcpy(qdi->info.name, "iOS UIView");
    strcpy(qdi->info.driver, "iOS");	    
    qdi->info.dir = PJMEDIA_DIR_RENDER;
    qdi->info.has_callback = PJ_FALSE;
    qdi->info.caps = PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW;
    
    if (NSClassFromString(@"AVCaptureSession")) {
	qdi = &qf->dev_info[qf->dev_count++];
	pj_bzero(qdi, sizeof(*qdi));
	strcpy(qdi->info.name, "iOS AVCapture");
	strcpy(qdi->info.driver, "iOS");	    
	qdi->info.dir = PJMEDIA_DIR_CAPTURE;
	qdi->info.has_callback = PJ_TRUE;
    }

    for (i = 0; i < qf->dev_count; i++) {
	qdi = &qf->dev_info[i];
	qdi->info.fmt_cnt = PJ_ARRAY_SIZE(ios_fmts);	    
	qdi->info.caps |= PJMEDIA_VID_DEV_CAP_FORMAT;
	
	for (l = 0; l < PJ_ARRAY_SIZE(ios_fmts); l++) {
	    pjmedia_format *fmt = &qdi->info.fmt[l];
	    pjmedia_format_init_video(fmt,
				      ios_fmts[l].pjmedia_format,
				      DEFAULT_WIDTH,
				      DEFAULT_HEIGHT,
				      DEFAULT_FPS, 1);	
	}
    }
    
    PJ_LOG(4, (THIS_FILE, "iOS video initialized with %d devices",
	       qf->dev_count));
    
    return PJ_SUCCESS;
}

/* API: destroy factory */
static pj_status_t ios_factory_destroy(pjmedia_vid_dev_factory *f)
{
    struct ios_factory *qf = (struct ios_factory*)f;
    pj_pool_t *pool = qf->pool;

    qf->pool = NULL;
    pj_pool_release(pool);

    return PJ_SUCCESS;
}

/* API: refresh the list of devices */
static pj_status_t ios_factory_refresh(pjmedia_vid_dev_factory *f)
{
    PJ_UNUSED_ARG(f);
    return PJ_SUCCESS;
}

/* API: get number of devices */
static unsigned ios_factory_get_dev_count(pjmedia_vid_dev_factory *f)
{
    struct ios_factory *qf = (struct ios_factory*)f;
    return qf->dev_count;
}

/* API: get device info */
static pj_status_t ios_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					    unsigned index,
					    pjmedia_vid_dev_info *info)
{
    struct ios_factory *qf = (struct ios_factory*)f;

    PJ_ASSERT_RETURN(index < qf->dev_count, PJMEDIA_EVID_INVDEV);

    pj_memcpy(info, &qf->dev_info[index].info, sizeof(*info));

    return PJ_SUCCESS;
}

/* API: create default device parameter */
static pj_status_t ios_factory_default_param(pj_pool_t *pool,
					     pjmedia_vid_dev_factory *f,
					     unsigned index,
					     pjmedia_vid_dev_param *param)
{
    struct ios_factory *qf = (struct ios_factory*)f;
    struct ios_dev_info *di = &qf->dev_info[index];

    PJ_ASSERT_RETURN(index < qf->dev_count, PJMEDIA_EVID_INVDEV);

    PJ_UNUSED_ARG(pool);

    pj_bzero(param, sizeof(*param));
    if (di->info.dir & PJMEDIA_DIR_CAPTURE) {
	param->dir = PJMEDIA_DIR_CAPTURE;
	param->cap_id = index;
	param->rend_id = PJMEDIA_VID_INVALID_DEV;
    } else if (di->info.dir & PJMEDIA_DIR_RENDER) {
	param->dir = PJMEDIA_DIR_RENDER;
	param->rend_id = index;
	param->cap_id = PJMEDIA_VID_INVALID_DEV;
    } else {
	return PJMEDIA_EVID_INVDEV;
    }
    
    param->flags = PJMEDIA_VID_DEV_CAP_FORMAT;
    param->clock_rate = DEFAULT_CLOCK_RATE;
    pj_memcpy(&param->fmt, &di->info.fmt[0], sizeof(param->fmt));

    return PJ_SUCCESS;
}

@implementation VOutDelegate
- (void)update_image
{    
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    
    /* Create a device-dependent RGB color space */
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB(); 
    
    /* Create a bitmap graphics context with the sample buffer data */
    CGContextRef context = 
	CGBitmapContextCreate(stream->buf, stream->size.w, stream->size.h, 8,
			      stream->bytes_per_row, colorSpace,
			      kCGBitmapByteOrder32Little |
			      kCGImageAlphaPremultipliedFirst);
    
    /**
     * Create a Quartz image from the pixel data in the bitmap graphics
     * context
     */
    CGImageRef quartzImage = CGBitmapContextCreateImage(context); 
    
    /* Free up the context and color space */
    CGContextRelease(context); 
    CGColorSpaceRelease(colorSpace);
    
    /* Create an image object from the Quartz image */
    UIImage *image = [UIImage imageWithCGImage:quartzImage scale:1.0 
			      orientation:UIImageOrientationRight];
    
    /* Release the Quartz image */
    CGImageRelease(quartzImage);
    
    dispatch_async(dispatch_get_main_queue(),
                   ^{[stream->imgView setImage:image];});
    /*
    [stream->imgView performSelectorOnMainThread:@selector(setImage:)
		     withObject:image waitUntilDone:NO];
     */
    
    [pool release];
}    

- (void)captureOutput:(AVCaptureOutput *)captureOutput 
		      didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
		      fromConnection:(AVCaptureConnection *)connection
{
    pjmedia_frame frame;
    CVImageBufferRef imageBuffer;

    if (!sampleBuffer)
	return;
    
    /* Get a CMSampleBuffer's Core Video image buffer for the media data */
    imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer); 
    
    /* Lock the base address of the pixel buffer */
    CVPixelBufferLockBaseAddress(imageBuffer, 0); 
    
    frame.type = PJMEDIA_FRAME_TYPE_VIDEO;
    frame.buf = CVPixelBufferGetBaseAddress(imageBuffer);
    frame.size = stream->frame_size;
    frame.bit_info = 0;
    frame.timestamp.u64 = stream->frame_ts.u64;
    
    if (stream->vid_cb.capture_cb)
        (*stream->vid_cb.capture_cb)(&stream->base, stream->user_data, &frame);

    stream->frame_ts.u64 += stream->ts_inc;
    
    /* Unlock the pixel buffer */
    CVPixelBufferUnlockBaseAddress(imageBuffer,0);
}
@end

static ios_fmt_info* get_ios_format_info(pjmedia_format_id id)
{
    unsigned i;
    
    for (i = 0; i < PJ_ARRAY_SIZE(ios_fmts); i++) {
        if (ios_fmts[i].pjmedia_format == id)
            return &ios_fmts[i];
    }
    
    return NULL;
}

/* API: create stream */
static pj_status_t ios_factory_create_stream(
					pjmedia_vid_dev_factory *f,
					pjmedia_vid_dev_param *param,
					const pjmedia_vid_dev_cb *cb,
					void *user_data,
					pjmedia_vid_dev_stream **p_vid_strm)
{
    struct ios_factory *qf = (struct ios_factory*)f;
    pj_pool_t *pool;
    struct ios_stream *strm;
    const pjmedia_video_format_detail *vfd;
    const pjmedia_video_format_info *vfi;
    pj_status_t status = PJ_SUCCESS;
    ios_fmt_info *ifi = get_ios_format_info(param->fmt.id);
    NSError *error;

    PJ_ASSERT_RETURN(f && param && p_vid_strm, PJ_EINVAL);
    PJ_ASSERT_RETURN(param->fmt.type == PJMEDIA_TYPE_VIDEO &&
		     param->fmt.detail_type == PJMEDIA_FORMAT_DETAIL_VIDEO &&
                     (param->dir == PJMEDIA_DIR_CAPTURE ||
                     param->dir == PJMEDIA_DIR_RENDER),
		     PJ_EINVAL);

    if (!(ifi = get_ios_format_info(param->fmt.id)))
        return PJMEDIA_EVID_BADFORMAT;
    
    vfi = pjmedia_get_video_format_info(NULL, param->fmt.id);
    if (!vfi)
        return PJMEDIA_EVID_BADFORMAT;

    /* Create and Initialize stream descriptor */
    pool = pj_pool_create(qf->pf, "ios-dev", 4000, 4000, NULL);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    strm = PJ_POOL_ZALLOC_T(pool, struct ios_stream);
    pj_memcpy(&strm->param, param, sizeof(*param));
    strm->pool = pool;
    pj_memcpy(&strm->vid_cb, cb, sizeof(*cb));
    strm->user_data = user_data;

    vfd = pjmedia_format_get_video_format_detail(&strm->param.fmt, PJ_TRUE);
    pj_memcpy(&strm->size, &vfd->size, sizeof(vfd->size));
    strm->bpp = vfi->bpp;
    strm->bytes_per_row = strm->size.w * strm->bpp / 8;
    strm->frame_size = strm->bytes_per_row * strm->size.h;
    strm->ts_inc = PJMEDIA_SPF2(param->clock_rate, &vfd->fps, 1);

    if (param->dir & PJMEDIA_DIR_CAPTURE) {
        /* Create capture stream here */
	strm->cap_session = [[AVCaptureSession alloc] init];
	if (!strm->cap_session) {
	    status = PJ_ENOMEM;
	    goto on_error;
	}
	strm->cap_session.sessionPreset = AVCaptureSessionPresetMedium;
	
	/* Open video device */
	AVCaptureDevice *videoDevice = 
	    [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
	if (!videoDevice) {
	    status = PJMEDIA_EVID_SYSERR;
	    goto on_error;
	}
	
	/* Add the video device to the session as a device input */	
	strm->dev_input = [AVCaptureDeviceInput 
			   deviceInputWithDevice:videoDevice
			   error: &error];
	if (!strm->dev_input) {
	    status = PJMEDIA_EVID_SYSERR;
	    goto on_error;
	}
	[strm->cap_session addInput:strm->dev_input];
	
	strm->video_output = [[[AVCaptureVideoDataOutput alloc] init]
			      autorelease];
	if (!strm->video_output) {
	    status = PJMEDIA_EVID_SYSERR;
	    goto on_error;
	}
	[strm->cap_session addOutput:strm->video_output];
	
	/* Configure the video output */
	strm->vout_delegate = [VOutDelegate alloc];
	strm->vout_delegate->stream = strm;
	dispatch_queue_t queue = dispatch_queue_create("myQueue", NULL);
	[strm->video_output setSampleBufferDelegate:strm->vout_delegate
			    queue:queue];
	dispatch_release(queue);	
	
	strm->video_output.videoSettings =
	    [NSDictionary dictionaryWithObjectsAndKeys:
			  [NSNumber numberWithInt:ifi->ios_format],
			  kCVPixelBufferPixelFormatTypeKey,
			  [NSNumber numberWithInt: vfd->size.w],
			  kCVPixelBufferWidthKey,
			  [NSNumber numberWithInt: vfd->size.h],
			  kCVPixelBufferHeightKey, nil];
	strm->video_output.minFrameDuration = CMTimeMake(vfd->fps.denum,
							 vfd->fps.num);	
    } else if (param->dir & PJMEDIA_DIR_RENDER) {
        /* Create renderer stream here */
	/* Get the main window */
	UIWindow *window = [[UIApplication sharedApplication] keyWindow];
	
	if (param->flags & PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW &&
            param->window.info.ios.window)
	    window = (UIWindow *)param->window.info.ios.window;
	
	pj_assert(window);
	strm->imgView = [[UIImageView alloc] initWithFrame:[window bounds]];
	if (!strm->imgView) {
	    status = PJ_ENOMEM;
	    goto on_error;
	}
	[window addSubview:strm->imgView];
	
	if (!strm->vout_delegate) {
	    strm->vout_delegate = [VOutDelegate alloc];
	    strm->vout_delegate->stream = strm;
	}
        
        strm->render_queue = dispatch_queue_create("com.pjsip.render_queue",
                                                   NULL);
        if (!strm->render_queue)
            goto on_error;
	
	strm->buf = pj_pool_alloc(pool, strm->frame_size);
    }    
    
    /* Apply the remaining settings */
    /*    
     if (param->flags & PJMEDIA_VID_DEV_CAP_INPUT_SCALE) {
	ios_stream_set_cap(&strm->base,
			  PJMEDIA_VID_DEV_CAP_INPUT_SCALE,
			  &param->fmt);
     }
     */
    /* Done */
    strm->base.op = &stream_op;
    *p_vid_strm = &strm->base;
    
    return PJ_SUCCESS;
    
on_error:
    ios_stream_destroy((pjmedia_vid_dev_stream *)strm);
    
    return status;
}

/* API: Get stream info. */
static pj_status_t ios_stream_get_param(pjmedia_vid_dev_stream *s,
				        pjmedia_vid_dev_param *pi)
{
    struct ios_stream *strm = (struct ios_stream*)s;

    PJ_ASSERT_RETURN(strm && pi, PJ_EINVAL);

    pj_memcpy(pi, &strm->param, sizeof(*pi));

/*    if (ios_stream_get_cap(s, PJMEDIA_VID_DEV_CAP_INPUT_SCALE,
                            &pi->fmt.info_size) == PJ_SUCCESS)
    {
        pi->flags |= PJMEDIA_VID_DEV_CAP_INPUT_SCALE;
    }
*/
    return PJ_SUCCESS;
}

/* API: get capability */
static pj_status_t ios_stream_get_cap(pjmedia_vid_dev_stream *s,
				      pjmedia_vid_dev_cap cap,
				      void *pval)
{
    struct ios_stream *strm = (struct ios_stream*)s;

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
static pj_status_t ios_stream_set_cap(pjmedia_vid_dev_stream *s,
				      pjmedia_vid_dev_cap cap,
				      const void *pval)
{
    struct ios_stream *strm = (struct ios_stream*)s;

    PJ_UNUSED_ARG(strm);

    PJ_ASSERT_RETURN(s && pval, PJ_EINVAL);

    if (cap==PJMEDIA_VID_DEV_CAP_INPUT_SCALE)
    {
	return PJ_SUCCESS;
    }

    return PJMEDIA_EVID_INVCAP;
}

/* API: Start stream. */
static pj_status_t ios_stream_start(pjmedia_vid_dev_stream *strm)
{
    struct ios_stream *stream = (struct ios_stream*)strm;

    PJ_UNUSED_ARG(stream);

    PJ_LOG(4, (THIS_FILE, "Starting ios video stream"));

    if (stream->cap_session) {
	[stream->cap_session startRunning];
    
	if (![stream->cap_session isRunning])
	    return PJ_EUNKNOWN;
    }
    
    return PJ_SUCCESS;
}


/* API: Put frame from stream */
static pj_status_t ios_stream_put_frame(pjmedia_vid_dev_stream *strm,
					const pjmedia_frame *frame)
{
    struct ios_stream *stream = (struct ios_stream*)strm;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    
    pj_assert(stream->frame_size >= frame->size);
    pj_memcpy(stream->buf, frame->buf, frame->size);
    /* Perform video display in a background thread */
/*   
    [stream->vout_delegate update_image];
    [NSThread detachNewThreadSelector:@selector(update_image)
	      toTarget:stream->vout_delegate withObject:nil];
*/
    dispatch_async(stream->render_queue,
                   ^{[stream->vout_delegate update_image];});
    
    [pool release];
    
    return PJ_SUCCESS;
}

/* API: Stop stream. */
static pj_status_t ios_stream_stop(pjmedia_vid_dev_stream *strm)
{
    struct ios_stream *stream = (struct ios_stream*)strm;

    PJ_UNUSED_ARG(stream);

    PJ_LOG(4, (THIS_FILE, "Stopping ios video stream"));

    if (stream->cap_session && [stream->cap_session isRunning])
	[stream->cap_session stopRunning];
    
    return PJ_SUCCESS;
}


/* API: Destroy stream. */
static pj_status_t ios_stream_destroy(pjmedia_vid_dev_stream *strm)
{
    struct ios_stream *stream = (struct ios_stream*)strm;

    PJ_ASSERT_RETURN(stream != NULL, PJ_EINVAL);

    ios_stream_stop(strm);
    
    if (stream->imgView) {
	[stream->imgView removeFromSuperview];
	[stream->imgView release];
	stream->imgView = NULL;
    }

    if (stream->cap_session) {
	[stream->cap_session release];
	stream->cap_session = NULL;
    }    
/*    if (stream->dev_input) {
	[stream->dev_input release];
	stream->dev_input = NULL;
    }
*/ 
    if (stream->vout_delegate) {
	[stream->vout_delegate release];
	stream->vout_delegate = NULL;
    }
/*    if (stream->video_output) {
	[stream->video_output release];
	stream->video_output = NULL;
    }
*/
    if (stream->render_queue) {
        dispatch_release(stream->render_queue);
        stream->render_queue = NULL;
    }

    pj_pool_release(stream->pool);

    return PJ_SUCCESS;
}

#endif
#endif	/* PJMEDIA_VIDEO_DEV_HAS_IOS */
