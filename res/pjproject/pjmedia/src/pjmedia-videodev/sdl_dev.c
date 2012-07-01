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

#if defined(PJMEDIA_VIDEO_DEV_HAS_SDL) && PJMEDIA_VIDEO_DEV_HAS_SDL != 0

#include <SDL.h>
#include <SDL_syswm.h>
#if PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL
#   include "SDL_opengl.h"
#   define OPENGL_DEV_IDX 1
#endif /* PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL */

#if !(SDL_VERSION_ATLEAST(1,3,0))
#   error "SDL 1.3 or later is required"
#endif

#if defined(PJ_DARWINOS) && PJ_DARWINOS!=0
#   include "TargetConditionals.h"
#   include <Foundation/Foundation.h>
#endif

#define THIS_FILE		"sdl_dev.c"
#define DEFAULT_CLOCK_RATE	90000
#define DEFAULT_WIDTH		640
#define DEFAULT_HEIGHT		480
#define DEFAULT_FPS		25

typedef struct sdl_fmt_info
{
    pjmedia_format_id   fmt_id;
    Uint32              sdl_format;
    Uint32              Rmask;
    Uint32              Gmask;
    Uint32              Bmask;
    Uint32              Amask;
} sdl_fmt_info;

static sdl_fmt_info sdl_fmts[] =
{
#if PJ_IS_BIG_ENDIAN
    {PJMEDIA_FORMAT_RGBA,  (Uint32)SDL_PIXELFORMAT_RGBA8888,
     0xFF000000, 0xFF0000, 0xFF00, 0xFF} ,
    {PJMEDIA_FORMAT_RGB24, (Uint32)SDL_PIXELFORMAT_RGB24,
     0xFF0000, 0xFF00, 0xFF, 0} ,
    {PJMEDIA_FORMAT_BGRA,  (Uint32)SDL_PIXELFORMAT_BGRA8888,
     0xFF00, 0xFF0000, 0xFF000000, 0xFF} ,
#else /* PJ_IS_BIG_ENDIAN */
    {PJMEDIA_FORMAT_RGBA,  (Uint32)SDL_PIXELFORMAT_ABGR8888,
     0xFF, 0xFF00, 0xFF0000, 0xFF000000} ,
    {PJMEDIA_FORMAT_RGB24, (Uint32)SDL_PIXELFORMAT_BGR24,
     0xFF, 0xFF00, 0xFF0000, 0} ,
    {PJMEDIA_FORMAT_BGRA,  (Uint32)SDL_PIXELFORMAT_ARGB8888,
     0xFF0000, 0xFF00, 0xFF, 0xFF000000} ,
#endif /* PJ_IS_BIG_ENDIAN */

    {PJMEDIA_FORMAT_DIB , (Uint32)SDL_PIXELFORMAT_RGB24,
     0xFF0000, 0xFF00, 0xFF, 0} ,

    {PJMEDIA_FORMAT_YUY2, SDL_YUY2_OVERLAY, 0, 0, 0, 0} ,
    {PJMEDIA_FORMAT_UYVY, SDL_UYVY_OVERLAY, 0, 0, 0, 0} ,
    {PJMEDIA_FORMAT_YVYU, SDL_YVYU_OVERLAY, 0, 0, 0, 0} ,
    {PJMEDIA_FORMAT_I420, SDL_IYUV_OVERLAY, 0, 0, 0, 0} ,
    {PJMEDIA_FORMAT_YV12, SDL_YV12_OVERLAY, 0, 0, 0, 0} ,
    {PJMEDIA_FORMAT_I420JPEG, SDL_IYUV_OVERLAY, 0, 0, 0, 0} ,
    {PJMEDIA_FORMAT_I422JPEG, SDL_YV12_OVERLAY, 0, 0, 0, 0} ,
};

/* sdl_ device info */
struct sdl_dev_info
{
    pjmedia_vid_dev_info	 info;
};

/* Linked list of streams */
struct stream_list
{
    PJ_DECL_LIST_MEMBER(struct stream_list);
    struct sdl_stream	*stream;
};

#define INITIAL_MAX_JOBS 64
#define JOB_QUEUE_INC_FACTOR 2

typedef pj_status_t (*job_func_ptr)(void *data);

typedef struct job {
    job_func_ptr    func;
    void           *data;
    unsigned        flags;
    pj_status_t     retval;
} job;

#if defined(PJ_DARWINOS) && PJ_DARWINOS!=0
@interface JQDelegate: NSObject
{
    @public
    job *pjob;
}

- (void)run_job;
@end

@implementation JQDelegate
- (void)run_job
{
    pjob->retval = (*pjob->func)(pjob->data);
}
@end
#endif /* PJ_DARWINOS */

typedef struct job_queue {
    pj_pool_t      *pool;
    job           **jobs;
    pj_sem_t      **job_sem;
    pj_sem_t      **old_sem;
    pj_mutex_t     *mutex;
    pj_thread_t    *thread;
    pj_sem_t       *sem;

    unsigned        size;
    unsigned        head, tail;
    pj_bool_t	    is_full;
    pj_bool_t       is_quitting;
} job_queue;

/* sdl_ factory */
struct sdl_factory
{
    pjmedia_vid_dev_factory	 base;
    pj_pool_t			*pool;
    pj_pool_factory		*pf;

    unsigned			 dev_count;
    struct sdl_dev_info	        *dev_info;
    job_queue                   *jq;

    pj_thread_t			*sdl_thread;        /**< SDL thread.        */
    pj_sem_t                    *sem;
    pj_mutex_t			*mutex;
    struct stream_list		 streams;
    pj_bool_t                    is_quitting;
    pj_thread_desc 		 thread_desc;
    pj_thread_t 		*ev_thread;
};

/* Video stream. */
struct sdl_stream
{
    pjmedia_vid_dev_stream	 base;		    /**< Base stream	    */
    pjmedia_vid_dev_param	 param;		    /**< Settings	    */
    pj_pool_t			*pool;              /**< Memory pool.       */

    pjmedia_vid_dev_cb		 vid_cb;            /**< Stream callback.   */
    void			*user_data;         /**< Application data.  */

    struct sdl_factory          *sf;
    const pjmedia_frame         *frame;
    pj_bool_t			 is_running;
    pj_timestamp		 last_ts;
    struct stream_list		 list_entry;

    SDL_Window                  *window;            /**< Display window.    */
    SDL_Renderer                *renderer;          /**< Display renderer.  */
    SDL_Texture                 *scr_tex;           /**< Screen texture.    */
    int                          pitch;             /**< Pitch value.       */
    SDL_Rect			 rect;              /**< Frame rectangle.   */
    SDL_Rect			 dstrect;           /**< Display rectangle. */
#if PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL
    SDL_GLContext               *gl_context;
    GLuint			 texture;
#endif /* PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL */

    pjmedia_video_apply_fmt_param vafp;
};

/* Prototypes */
static pj_status_t sdl_factory_init(pjmedia_vid_dev_factory *f);
static pj_status_t sdl_factory_destroy(pjmedia_vid_dev_factory *f);
static pj_status_t sdl_factory_refresh(pjmedia_vid_dev_factory *f);
static unsigned    sdl_factory_get_dev_count(pjmedia_vid_dev_factory *f);
static pj_status_t sdl_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					    unsigned index,
					    pjmedia_vid_dev_info *info);
static pj_status_t sdl_factory_default_param(pj_pool_t *pool,
                                             pjmedia_vid_dev_factory *f,
					     unsigned index,
					     pjmedia_vid_dev_param *param);
static pj_status_t sdl_factory_create_stream(
					pjmedia_vid_dev_factory *f,
					pjmedia_vid_dev_param *param,
					const pjmedia_vid_dev_cb *cb,
					void *user_data,
					pjmedia_vid_dev_stream **p_vid_strm);

static pj_status_t sdl_stream_get_param(pjmedia_vid_dev_stream *strm,
					pjmedia_vid_dev_param *param);
static pj_status_t sdl_stream_get_cap(pjmedia_vid_dev_stream *strm,
				      pjmedia_vid_dev_cap cap,
				      void *value);
static pj_status_t sdl_stream_set_cap(pjmedia_vid_dev_stream *strm,
				      pjmedia_vid_dev_cap cap,
				      const void *value);
static pj_status_t sdl_stream_put_frame(pjmedia_vid_dev_stream *strm,
                                        const pjmedia_frame *frame);
static pj_status_t sdl_stream_start(pjmedia_vid_dev_stream *strm);
static pj_status_t sdl_stream_stop(pjmedia_vid_dev_stream *strm);
static pj_status_t sdl_stream_destroy(pjmedia_vid_dev_stream *strm);

static pj_status_t resize_disp(struct sdl_stream *strm,
                               pjmedia_rect_size *new_disp_size);
static pj_status_t sdl_destroy_all(void *data);

/* Job queue prototypes */
static pj_status_t job_queue_create(pj_pool_t *pool, job_queue **pjq);
static pj_status_t job_queue_post_job(job_queue *jq, job_func_ptr func,
				      void *data, unsigned flags,
				      pj_status_t *retval);
static pj_status_t job_queue_destroy(job_queue *jq);

/* Operations */
static pjmedia_vid_dev_factory_op factory_op =
{
    &sdl_factory_init,
    &sdl_factory_destroy,
    &sdl_factory_get_dev_count,
    &sdl_factory_get_dev_info,
    &sdl_factory_default_param,
    &sdl_factory_create_stream,
    &sdl_factory_refresh
};

static pjmedia_vid_dev_stream_op stream_op =
{
    &sdl_stream_get_param,
    &sdl_stream_get_cap,
    &sdl_stream_set_cap,
    &sdl_stream_start,
    NULL,
    &sdl_stream_put_frame,
    &sdl_stream_stop,
    &sdl_stream_destroy
};


/****************************************************************************
 * Factory operations
 */
/*
 * Init sdl_ video driver.
 */
pjmedia_vid_dev_factory* pjmedia_sdl_factory(pj_pool_factory *pf)
{
    struct sdl_factory *f;
    pj_pool_t *pool;

    pool = pj_pool_create(pf, "sdl video", 1000, 1000, NULL);
    f = PJ_POOL_ZALLOC_T(pool, struct sdl_factory);
    f->pf = pf;
    f->pool = pool;
    f->base.op = &factory_op;

    return &f->base;
}

static pj_status_t sdl_init(void * data)
{
    PJ_UNUSED_ARG(data);

    if (SDL_Init(SDL_INIT_VIDEO)) {
        PJ_LOG(3, (THIS_FILE, "Failed initializing SDL"));
        return PJMEDIA_EVID_INIT;
    }

    return PJ_SUCCESS;
}

static struct sdl_stream* find_stream(struct sdl_factory *sf,
                                      Uint32 windowID,
                                      pjmedia_event *pevent)
{
    struct stream_list *it, *itBegin;
    struct sdl_stream *strm = NULL;

    itBegin = &sf->streams;
    for (it = itBegin->next; it != itBegin; it = it->next) {
        if (SDL_GetWindowID(it->stream->window) == windowID)
        {
            strm = it->stream;
            break;
        }
    }
 
    if (strm)
        pjmedia_event_init(pevent, PJMEDIA_EVENT_NONE, &strm->last_ts,
		           strm);

    return strm;
}

static pj_status_t handle_event(void *data)
{
    struct sdl_factory *sf = (struct sdl_factory*)data;
    SDL_Event sevent;

    if (!pj_thread_is_registered())
	pj_thread_register("sdl_ev", sf->thread_desc, &sf->ev_thread);

    while (SDL_PollEvent(&sevent)) {
        struct sdl_stream *strm = NULL;
        pjmedia_event pevent;

        pj_mutex_lock(sf->mutex);
        pevent.type = PJMEDIA_EVENT_NONE;
	switch(sevent.type) {
        case SDL_MOUSEBUTTONDOWN:
            strm = find_stream(sf, sevent.button.windowID, &pevent);
            pevent.type = PJMEDIA_EVENT_MOUSE_BTN_DOWN;
            break;
        case SDL_WINDOWEVENT:
            strm = find_stream(sf, sevent.window.windowID, &pevent);
            switch (sevent.window.event) {
            case SDL_WINDOWEVENT_RESIZED:
                pevent.type = PJMEDIA_EVENT_WND_RESIZED;
                pevent.data.wnd_resized.new_size.w =
                    sevent.window.data1;
                pevent.data.wnd_resized.new_size.h =
                    sevent.window.data2;
                break;
            case SDL_WINDOWEVENT_CLOSE:
                pevent.type = PJMEDIA_EVENT_WND_CLOSING;
                break;
            }
            break;
        default:
            break;
	}

        if (strm && pevent.type != PJMEDIA_EVENT_NONE) {
            pj_status_t status;

	    pjmedia_event_publish(NULL, strm, &pevent, 0);

	    switch (pevent.type) {
	    case PJMEDIA_EVENT_WND_RESIZED:
                status = resize_disp(strm, &pevent.data.wnd_resized.new_size);
                if (status != PJ_SUCCESS)
                    PJ_LOG(3, (THIS_FILE, "Failed resizing the display."));
		break;
	    case PJMEDIA_EVENT_WND_CLOSING:
		if (pevent.data.wnd_closing.cancel) {
		    /* Cancel the closing operation */
		    break;
		}

		/* Proceed to cleanup SDL. App must still call
		 * pjmedia_dev_stream_destroy() when getting WND_CLOSED
		 * event
		 */
		sdl_stream_stop(&strm->base);
                sdl_destroy_all(strm);
                pjmedia_event_init(&pevent, PJMEDIA_EVENT_WND_CLOSED,
                                   &strm->last_ts, strm);
                pjmedia_event_publish(NULL, strm, &pevent, 0);

                /*
                 * Note: don't access the stream after this point, it
                 * might have been destroyed
                 */
                break;
	    default:
		/* Just to prevent gcc warning about unused enums */
		break;
	    }
        }

        pj_mutex_unlock(sf->mutex);
    }

    return PJ_SUCCESS;
}

static int sdl_ev_thread(void *data)
{
    struct sdl_factory *sf = (struct sdl_factory*)data;

    while(1) {
        pj_status_t status;

        pj_mutex_lock(sf->mutex);
        if (pj_list_empty(&sf->streams)) {
            pj_mutex_unlock(sf->mutex);
            /* Wait until there is any stream. */
            pj_sem_wait(sf->sem);
        } else
            pj_mutex_unlock(sf->mutex);

        if (sf->is_quitting)
            break;

        job_queue_post_job(sf->jq, handle_event, sf, 0, &status);

        pj_thread_sleep(50);
    }

    return 0;
}

static pj_status_t sdl_quit(void *data)
{
    PJ_UNUSED_ARG(data);
    SDL_Quit();
    return PJ_SUCCESS;
}

/* API: init factory */
static pj_status_t sdl_factory_init(pjmedia_vid_dev_factory *f)
{
    struct sdl_factory *sf = (struct sdl_factory*)f;
    struct sdl_dev_info *ddi;
    unsigned i, j;
    pj_status_t status;
    SDL_version version;

    status = job_queue_create(sf->pool, &sf->jq);
    if (status != PJ_SUCCESS)
        return PJMEDIA_EVID_INIT;

    job_queue_post_job(sf->jq, sdl_init, NULL, 0, &status);
    if (status != PJ_SUCCESS)
        return status;

    pj_list_init(&sf->streams);
    status = pj_mutex_create_recursive(sf->pool, "sdl_factory",
				       &sf->mutex);
    if (status != PJ_SUCCESS)
	return status;

    status = pj_sem_create(sf->pool, NULL, 0, 1, &sf->sem);
    if (status != PJ_SUCCESS)
	return status;

    /* Create event handler thread. */
    status = pj_thread_create(sf->pool, "sdl_thread", sdl_ev_thread,
			      sf, 0, 0, &sf->sdl_thread);
    if (status != PJ_SUCCESS)
        return status;

    sf->dev_count = 1;
#if PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL
    sf->dev_count++;
#endif /* PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL */
    sf->dev_info = (struct sdl_dev_info*)
		   pj_pool_calloc(sf->pool, sf->dev_count,
				  sizeof(struct sdl_dev_info));

    ddi = &sf->dev_info[0];
    pj_bzero(ddi, sizeof(*ddi));
    strncpy(ddi->info.name, "SDL renderer", sizeof(ddi->info.name));
    ddi->info.name[sizeof(ddi->info.name)-1] = '\0';
    ddi->info.fmt_cnt = PJ_ARRAY_SIZE(sdl_fmts);

#if PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL
    ddi = &sf->dev_info[OPENGL_DEV_IDX];
    pj_bzero(ddi, sizeof(*ddi));
    strncpy(ddi->info.name, "SDL openGL renderer", sizeof(ddi->info.name));
    ddi->info.name[sizeof(ddi->info.name)-1] = '\0';
    ddi->info.fmt_cnt = 1;
#endif /* PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL */

    for (i = 0; i < sf->dev_count; i++) {
        ddi = &sf->dev_info[i];
        strncpy(ddi->info.driver, "SDL", sizeof(ddi->info.driver));
        ddi->info.driver[sizeof(ddi->info.driver)-1] = '\0';
        ddi->info.dir = PJMEDIA_DIR_RENDER;
        ddi->info.has_callback = PJ_FALSE;
        ddi->info.caps = PJMEDIA_VID_DEV_CAP_FORMAT |
                         PJMEDIA_VID_DEV_CAP_OUTPUT_RESIZE;
        ddi->info.caps |= PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW;
        ddi->info.caps |= PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW_FLAGS;

        for (j = 0; j < ddi->info.fmt_cnt; j++) {
            pjmedia_format *fmt = &ddi->info.fmt[j];
            pjmedia_format_init_video(fmt, sdl_fmts[j].fmt_id,
                                      DEFAULT_WIDTH, DEFAULT_HEIGHT,
                                      DEFAULT_FPS, 1);
        }
    }

    SDL_VERSION(&version);
    PJ_LOG(4, (THIS_FILE, "SDL %d.%d initialized",
			  version.major, version.minor));

    return PJ_SUCCESS;
}

/* API: destroy factory */
static pj_status_t sdl_factory_destroy(pjmedia_vid_dev_factory *f)
{
    struct sdl_factory *sf = (struct sdl_factory*)f;
    pj_pool_t *pool = sf->pool;
    pj_status_t status;

    pj_assert(pj_list_empty(&sf->streams));

    sf->is_quitting = PJ_TRUE;
    if (sf->sdl_thread) {
        pj_sem_post(sf->sem);
#if defined(PJ_DARWINOS) && PJ_DARWINOS!=0
        /* To prevent pj_thread_join() of getting stuck if we are in
         * the main thread and we haven't finished processing the job
         * posted by sdl_thread.
         */
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
#endif
        pj_thread_join(sf->sdl_thread);
        pj_thread_destroy(sf->sdl_thread);
    }

    if (sf->mutex) {
	pj_mutex_destroy(sf->mutex);
	sf->mutex = NULL;
    }

    if (sf->sem) {
        pj_sem_destroy(sf->sem);
        sf->sem = NULL;
    }

    job_queue_post_job(sf->jq, sdl_quit, NULL, 0, &status);
    job_queue_destroy(sf->jq);

    sf->pool = NULL;
    pj_pool_release(pool);

    return PJ_SUCCESS;
}

/* API: refresh the list of devices */
static pj_status_t sdl_factory_refresh(pjmedia_vid_dev_factory *f)
{
    PJ_UNUSED_ARG(f);
    return PJ_SUCCESS;
}

/* API: get number of devices */
static unsigned sdl_factory_get_dev_count(pjmedia_vid_dev_factory *f)
{
    struct sdl_factory *sf = (struct sdl_factory*)f;
    return sf->dev_count;
}

/* API: get device info */
static pj_status_t sdl_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					    unsigned index,
					    pjmedia_vid_dev_info *info)
{
    struct sdl_factory *sf = (struct sdl_factory*)f;

    PJ_ASSERT_RETURN(index < sf->dev_count, PJMEDIA_EVID_INVDEV);

    pj_memcpy(info, &sf->dev_info[index].info, sizeof(*info));

    return PJ_SUCCESS;
}

/* API: create default device parameter */
static pj_status_t sdl_factory_default_param(pj_pool_t *pool,
                                             pjmedia_vid_dev_factory *f,
					     unsigned index,
					     pjmedia_vid_dev_param *param)
{
    struct sdl_factory *sf = (struct sdl_factory*)f;
    struct sdl_dev_info *di = &sf->dev_info[index];

    PJ_ASSERT_RETURN(index < sf->dev_count, PJMEDIA_EVID_INVDEV);
    
    PJ_UNUSED_ARG(pool);

    pj_bzero(param, sizeof(*param));
    param->dir = PJMEDIA_DIR_RENDER;
    param->rend_id = index;
    param->cap_id = PJMEDIA_VID_INVALID_DEV;

    /* Set the device capabilities here */
    param->flags = PJMEDIA_VID_DEV_CAP_FORMAT;
    param->fmt.type = PJMEDIA_TYPE_VIDEO;
    param->clock_rate = DEFAULT_CLOCK_RATE;
    pj_memcpy(&param->fmt, &di->info.fmt[0], sizeof(param->fmt));

    return PJ_SUCCESS;
}

static sdl_fmt_info* get_sdl_format_info(pjmedia_format_id id)
{
    unsigned i;

    for (i = 0; i < sizeof(sdl_fmts)/sizeof(sdl_fmts[0]); i++) {
        if (sdl_fmts[i].fmt_id == id)
            return &sdl_fmts[i];
    }

    return NULL;
}

static pj_status_t sdl_destroy(void *data)
{
    struct sdl_stream *strm = (struct sdl_stream *)data;

#if PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL
    if (strm->texture) {
	glDeleteTextures(1, &strm->texture);
	strm->texture = 0;
    }
    if (strm->gl_context) {
        SDL_GL_DeleteContext(strm->gl_context);
        strm->gl_context = NULL;
    }
#endif /* PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL */
    if (strm->scr_tex) {
        SDL_DestroyTexture(strm->scr_tex);
        strm->scr_tex = NULL;
    }
    if (strm->renderer) {
        SDL_DestroyRenderer(strm->renderer);
        strm->renderer = NULL;
    }

    return PJ_SUCCESS;
}

static pj_status_t sdl_destroy_all(void *data)
{
    struct sdl_stream *strm = (struct sdl_stream *)data;

    sdl_destroy(data);
#if !defined(TARGET_OS_IPHONE) || TARGET_OS_IPHONE == 0
    if (strm->window &&
        !(strm->param.flags & PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW))
    {
        SDL_DestroyWindow(strm->window);
    }
    strm->window = NULL;
#endif /* TARGET_OS_IPHONE */

    return PJ_SUCCESS;
}

static pj_status_t sdl_create_rend(struct sdl_stream * strm,
                                   pjmedia_format *fmt)
{
    sdl_fmt_info *sdl_info;
    const pjmedia_video_format_info *vfi;
    pjmedia_video_format_detail *vfd;

    sdl_info = get_sdl_format_info(fmt->id);
    vfi = pjmedia_get_video_format_info(pjmedia_video_format_mgr_instance(),
                                        fmt->id);
    if (!vfi || !sdl_info)
        return PJMEDIA_EVID_BADFORMAT;

    strm->vafp.size = fmt->det.vid.size;
    strm->vafp.buffer = NULL;
    if (vfi->apply_fmt(vfi, &strm->vafp) != PJ_SUCCESS)
        return PJMEDIA_EVID_BADFORMAT;

    vfd = pjmedia_format_get_video_format_detail(fmt, PJ_TRUE);
    strm->rect.x = strm->rect.y = 0;
    strm->rect.w = (Uint16)vfd->size.w;
    strm->rect.h = (Uint16)vfd->size.h;
    if (strm->param.disp_size.w == 0)
        strm->param.disp_size.w = strm->rect.w;
    if (strm->param.disp_size.h == 0)
        strm->param.disp_size.h = strm->rect.h;
    strm->dstrect.x = strm->dstrect.y = 0;
    strm->dstrect.w = (Uint16)strm->param.disp_size.w;
    strm->dstrect.h = (Uint16)strm->param.disp_size.h;

    sdl_destroy(strm);

#if PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL
    if (strm->param.rend_id == OPENGL_DEV_IDX) {
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    }
#endif /* PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL */

    if (!strm->window) {
        Uint32 flags = 0;
        
        if (strm->param.flags & PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW_FLAGS) {
            if (!(strm->param.window_flags & PJMEDIA_VID_DEV_WND_BORDER))
                flags |= SDL_WINDOW_BORDERLESS;
            if (strm->param.window_flags & PJMEDIA_VID_DEV_WND_RESIZABLE)
                flags |= SDL_WINDOW_RESIZABLE;
        } else {
            flags |= SDL_WINDOW_BORDERLESS;
        }

        if (!((strm->param.flags & PJMEDIA_VID_DEV_CAP_OUTPUT_HIDE) &&
            strm->param.window_hide))
        {
            flags |= SDL_WINDOW_SHOWN;
        } else {
            flags &= ~SDL_WINDOW_SHOWN;
            flags |= SDL_WINDOW_HIDDEN;
        }

#if PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL
        if (strm->param.rend_id == OPENGL_DEV_IDX)
            flags |= SDL_WINDOW_OPENGL;
#endif /* PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL */

        if (strm->param.flags & PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW) {
            /* Use the window supplied by the application. */
	    strm->window = SDL_CreateWindowFrom(
                               strm->param.window.info.window);
        } else {
            int x, y;

            x = y = SDL_WINDOWPOS_CENTERED;
            if (strm->param.flags & PJMEDIA_VID_DEV_CAP_OUTPUT_POSITION) {
                x = strm->param.window_pos.x;
                y = strm->param.window_pos.y;
            }

            /* Create the window where we will draw. */
            strm->window = SDL_CreateWindow("pjmedia-SDL video",
                                            x, y,
                                            strm->param.disp_size.w,
                                            strm->param.disp_size.h,
                                            flags);
        }
        if (!strm->window)
            return PJMEDIA_EVID_SYSERR;
    }

    /**
      * We must call SDL_CreateRenderer in order for draw calls to
      * affect this window.
      */
    strm->renderer = SDL_CreateRenderer(strm->window, -1, 0);
    if (!strm->renderer)
        return PJMEDIA_EVID_SYSERR;

#if PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL
    if (strm->param.rend_id == OPENGL_DEV_IDX) {
        strm->gl_context = SDL_GL_CreateContext(strm->window);
        if (!strm->gl_context)
            return PJMEDIA_EVID_SYSERR;
        SDL_GL_MakeCurrent(strm->window, strm->gl_context);

        /* Init some OpenGL settings */
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_TEXTURE_2D);
	
	/* Init the viewport */
	glViewport(0, 0, strm->param.disp_size.w, strm->param.disp_size.h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	
	glOrtho(0.0, (GLdouble)strm->param.disp_size.w,
                (GLdouble)strm->param.disp_size.h, 0.0, 0.0, 1.0);
	
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	
	/* Create a texture */
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
	glGenTextures(1, &strm->texture);

        if (!strm->texture)
            return PJMEDIA_EVID_SYSERR;
    } else
#endif /* PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL */
    {    
        strm->scr_tex = SDL_CreateTexture(strm->renderer, sdl_info->sdl_format,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          strm->rect.w, strm->rect.h);
        if (strm->scr_tex == NULL)
            return PJMEDIA_EVID_SYSERR;
    
        strm->pitch = strm->rect.w * SDL_BYTESPERPIXEL(sdl_info->sdl_format);
    }

    return PJ_SUCCESS;
}

static pj_status_t sdl_create(void *data)
{
    struct sdl_stream *strm = (struct sdl_stream *)data;
    return sdl_create_rend(strm, &strm->param.fmt);
}

static pj_status_t resize_disp(struct sdl_stream *strm,
                               pjmedia_rect_size *new_disp_size)
{
    pj_memcpy(&strm->param.disp_size, new_disp_size,
              sizeof(strm->param.disp_size));
    
    if (strm->scr_tex) {
        strm->dstrect.x = strm->dstrect.y = 0;
        strm->dstrect.w = (Uint16)strm->param.disp_size.w;
	strm->dstrect.h = (Uint16)strm->param.disp_size.h;
	SDL_RenderSetViewport(strm->renderer, &strm->dstrect);
    }
#if PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL
    else if (strm->param.rend_id == OPENGL_DEV_IDX) {
	sdl_create_rend(strm, &strm->param.fmt);
    }
#endif /* PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL */

    return PJ_SUCCESS;
}

static pj_status_t change_format(struct sdl_stream *strm,
                                 pjmedia_format *new_fmt)
{
    pj_status_t status;

    /* Recreate SDL renderer */
    status = sdl_create_rend(strm, (new_fmt? new_fmt :
				   &strm->param.fmt));
    if (status == PJ_SUCCESS && new_fmt)
        pjmedia_format_copy(&strm->param.fmt, new_fmt);

    return status;
}

static pj_status_t put_frame(void *data)
{
    struct sdl_stream *stream = (struct sdl_stream *)data;
    const pjmedia_frame *frame = stream->frame;

    if (stream->scr_tex) {
        SDL_UpdateTexture(stream->scr_tex, NULL, frame->buf, stream->pitch);
        SDL_RenderClear(stream->renderer);
        SDL_RenderCopy(stream->renderer, stream->scr_tex,
		       &stream->rect, &stream->dstrect);
        SDL_RenderPresent(stream->renderer);
    }
#if PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL
    else if (stream->param.rend_id == OPENGL_DEV_IDX && stream->texture) {
	glBindTexture(GL_TEXTURE_2D, stream->texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
		     stream->rect.w, stream->rect.h, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, frame->buf);
	glBegin(GL_TRIANGLE_STRIP);
	glTexCoord2f(0, 0); glVertex2i(0, 0);
	glTexCoord2f(1, 0); glVertex2i(stream->param.disp_size.w, 0);
	glTexCoord2f(0, 1); glVertex2i(0, stream->param.disp_size.h);
	glTexCoord2f(1, 1);
        glVertex2i(stream->param.disp_size.w, stream->param.disp_size.h);
	glEnd();
        SDL_GL_SwapWindow(stream->window);
    }
#endif /* PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL */

    return PJ_SUCCESS;
}

/* API: Put frame from stream */
static pj_status_t sdl_stream_put_frame(pjmedia_vid_dev_stream *strm,
					const pjmedia_frame *frame)
{
    struct sdl_stream *stream = (struct sdl_stream*)strm;
    pj_status_t status;

    stream->last_ts.u64 = frame->timestamp.u64;

    if (!stream->is_running)
	return PJ_EINVALIDOP;

    if (frame->size==0 || frame->buf==NULL ||
	frame->size < stream->vafp.framebytes)
	return PJ_SUCCESS;

    stream->frame = frame;
    job_queue_post_job(stream->sf->jq, put_frame, strm, 0, &status);
    
    return status;
}

/* API: create stream */
static pj_status_t sdl_factory_create_stream(
					pjmedia_vid_dev_factory *f,
					pjmedia_vid_dev_param *param,
					const pjmedia_vid_dev_cb *cb,
					void *user_data,
					pjmedia_vid_dev_stream **p_vid_strm)
{
    struct sdl_factory *sf = (struct sdl_factory*)f;
    pj_pool_t *pool;
    struct sdl_stream *strm;
    pj_status_t status;

    PJ_ASSERT_RETURN(param->dir == PJMEDIA_DIR_RENDER, PJ_EINVAL);

    /* Create and Initialize stream descriptor */
    pool = pj_pool_create(sf->pf, "sdl-dev", 1000, 1000, NULL);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    strm = PJ_POOL_ZALLOC_T(pool, struct sdl_stream);
    pj_memcpy(&strm->param, param, sizeof(*param));
    strm->pool = pool;
    strm->sf = sf;
    pj_memcpy(&strm->vid_cb, cb, sizeof(*cb));
    pj_list_init(&strm->list_entry);
    strm->list_entry.stream = strm;
    strm->user_data = user_data;

    /* Create render stream here */
    job_queue_post_job(sf->jq, sdl_create, strm, 0, &status);
    if (status != PJ_SUCCESS) {
        goto on_error;
    }
    pj_mutex_lock(strm->sf->mutex);
    if (pj_list_empty(&strm->sf->streams))
        pj_sem_post(strm->sf->sem);
    pj_list_insert_after(&strm->sf->streams, &strm->list_entry);
    pj_mutex_unlock(strm->sf->mutex);

    /* Done */
    strm->base.op = &stream_op;
    *p_vid_strm = &strm->base;

    return PJ_SUCCESS;

on_error:
    sdl_stream_destroy(&strm->base);
    return status;
}

/* API: Get stream info. */
static pj_status_t sdl_stream_get_param(pjmedia_vid_dev_stream *s,
					pjmedia_vid_dev_param *pi)
{
    struct sdl_stream *strm = (struct sdl_stream*)s;

    PJ_ASSERT_RETURN(strm && pi, PJ_EINVAL);

    pj_memcpy(pi, &strm->param, sizeof(*pi));

    if (sdl_stream_get_cap(s, PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW,
			   &pi->window) == PJ_SUCCESS)
    {
	pi->flags |= PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW;
    }
    if (sdl_stream_get_cap(s, PJMEDIA_VID_DEV_CAP_OUTPUT_POSITION,
			   &pi->window_pos) == PJ_SUCCESS)
    {
	pi->flags |= PJMEDIA_VID_DEV_CAP_OUTPUT_POSITION;
    }
    if (sdl_stream_get_cap(s, PJMEDIA_VID_DEV_CAP_OUTPUT_RESIZE,
			   &pi->disp_size) == PJ_SUCCESS)
    {
	pi->flags |= PJMEDIA_VID_DEV_CAP_OUTPUT_RESIZE;
    }
    if (sdl_stream_get_cap(s, PJMEDIA_VID_DEV_CAP_OUTPUT_HIDE,
			   &pi->window_hide) == PJ_SUCCESS)
    {
	pi->flags |= PJMEDIA_VID_DEV_CAP_OUTPUT_HIDE;
    }
    if (sdl_stream_get_cap(s, PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW_FLAGS,
			   &pi->window_flags) == PJ_SUCCESS)
    {
	pi->flags |= PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW_FLAGS;
    }

    return PJ_SUCCESS;
}

struct strm_cap {
    struct sdl_stream   *strm;
    pjmedia_vid_dev_cap  cap;
    union {
        void            *pval;
        const void      *cpval;
    } pval;
};

static pj_status_t get_cap(void *data)
{
    struct strm_cap *scap = (struct strm_cap *)data;
    struct sdl_stream *strm = scap->strm;
    pjmedia_vid_dev_cap cap = scap->cap;
    void *pval = scap->pval.pval;

    if (cap == PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW)
    {
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);

	if (SDL_GetWindowWMInfo(strm->window, &info)) {
	    pjmedia_vid_dev_hwnd *wnd = (pjmedia_vid_dev_hwnd *)pval;
	    if (0) { }
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
	    else if (info.subsystem == SDL_SYSWM_WINDOWS) {
		wnd->type = PJMEDIA_VID_DEV_HWND_TYPE_WINDOWS;
		wnd->info.win.hwnd = (void *)info.info.win.window;
	    }
#endif
#if defined(SDL_VIDEO_DRIVER_X11)
	    else if (info.subsystem == SDL_SYSWM_X11) {
		wnd->info.x11.window = (void *)info.info.x11.window;
		wnd->info.x11.display = (void *)info.info.x11.display;
	    }
#endif
#if defined(SDL_VIDEO_DRIVER_COCOA)
	    else if (info.subsystem == SDL_SYSWM_COCOA) {
		wnd->info.cocoa.window = (void *)info.info.cocoa.window;
	    }
#endif
#if defined(SDL_VIDEO_DRIVER_UIKIT)
	    else if (info.subsystem == SDL_SYSWM_UIKIT) {
		wnd->info.ios.window = (void *)info.info.uikit.window;
	    }
#endif
	    else {
		return PJMEDIA_EVID_INVCAP;
	    }
	    return PJ_SUCCESS;
	} else
	    return PJMEDIA_EVID_INVCAP;
    } else if (cap == PJMEDIA_VID_DEV_CAP_OUTPUT_POSITION) {
        SDL_GetWindowPosition(strm->window, &((pjmedia_coord *)pval)->x,
                              &((pjmedia_coord *)pval)->y);
	return PJ_SUCCESS;
    } else if (cap == PJMEDIA_VID_DEV_CAP_OUTPUT_RESIZE) {
        SDL_GetWindowSize(strm->window, (int *)&((pjmedia_rect_size *)pval)->w,
                          (int *)&((pjmedia_rect_size *)pval)->h);
	return PJ_SUCCESS;
    } else if (cap == PJMEDIA_VID_DEV_CAP_OUTPUT_HIDE) {
	Uint32 flag = SDL_GetWindowFlags(strm->window);
	*((pj_bool_t *)pval) = (flag & SDL_WINDOW_HIDDEN)? PJ_TRUE: PJ_FALSE;
	return PJ_SUCCESS;
    } else if (cap == PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW_FLAGS) {
	Uint32 flag = SDL_GetWindowFlags(strm->window);
        unsigned *wnd_flags = (unsigned *)pval;
        if (!(flag & SDL_WINDOW_BORDERLESS))
            *wnd_flags |= PJMEDIA_VID_DEV_WND_BORDER;
        if (flag & SDL_WINDOW_RESIZABLE)
            *wnd_flags |= PJMEDIA_VID_DEV_WND_RESIZABLE;
	return PJ_SUCCESS;
    }

    return PJMEDIA_EVID_INVCAP;
}

/* API: get capability */
static pj_status_t sdl_stream_get_cap(pjmedia_vid_dev_stream *s,
				      pjmedia_vid_dev_cap cap,
				      void *pval)
{
    struct sdl_stream *strm = (struct sdl_stream*)s;
    struct strm_cap scap;
    pj_status_t status;

    PJ_ASSERT_RETURN(s && pval, PJ_EINVAL);

    scap.strm = strm;
    scap.cap = cap;
    scap.pval.pval = pval;

    job_queue_post_job(strm->sf->jq, get_cap, &scap, 0, &status);

    return status;
}

static pj_status_t set_cap(void *data)
{
    struct strm_cap *scap = (struct strm_cap *)data;
    struct sdl_stream *strm = scap->strm;
    pjmedia_vid_dev_cap cap = scap->cap;
    const void *pval = scap->pval.cpval;

    if (cap == PJMEDIA_VID_DEV_CAP_OUTPUT_POSITION) {
        /**
         * Setting window's position when the window is hidden also sets
         * the window's flag to shown (while the window is, actually,
         * still hidden). This causes problems later when setting/querying
         * the window's visibility.
         * See ticket #1429 (http://trac.pjsip.org/repos/ticket/1429)
         */
	Uint32 flag = SDL_GetWindowFlags(strm->window);
	if (flag & SDL_WINDOW_HIDDEN)
            SDL_ShowWindow(strm->window);
        SDL_SetWindowPosition(strm->window, ((pjmedia_coord *)pval)->x,
                              ((pjmedia_coord *)pval)->y);
	if (flag & SDL_WINDOW_HIDDEN)
            SDL_HideWindow(strm->window);
	return PJ_SUCCESS;
    } else if (cap == PJMEDIA_VID_DEV_CAP_OUTPUT_HIDE) {
        if (*(pj_bool_t *)pval)
            SDL_HideWindow(strm->window);
        else
            SDL_ShowWindow(strm->window);
	return PJ_SUCCESS;
    } else if (cap == PJMEDIA_VID_DEV_CAP_FORMAT) {
        pj_status_t status;

        status = change_format(strm, (pjmedia_format *)pval);
	if (status != PJ_SUCCESS) {
	    pj_status_t status_;
	    
	    /**
	     * Failed to change the output format. Try to revert
	     * to its original format.
	     */
            status_ = change_format(strm, &strm->param.fmt);
	    if (status_ != PJ_SUCCESS) {
		/**
		 * This means that we failed to revert to our
		 * original state!
		 */
		status = PJMEDIA_EVID_ERR;
	    }
	}
	
	return status;
    } else if (cap == PJMEDIA_VID_DEV_CAP_OUTPUT_RESIZE) {
	pjmedia_rect_size *new_size = (pjmedia_rect_size *)pval;

	SDL_SetWindowSize(strm->window, new_size->w, new_size->h);
        return resize_disp(strm, new_size);
    }

    return PJMEDIA_EVID_INVCAP;
}

/* API: set capability */
static pj_status_t sdl_stream_set_cap(pjmedia_vid_dev_stream *s,
				      pjmedia_vid_dev_cap cap,
				      const void *pval)
{
    struct sdl_stream *strm = (struct sdl_stream*)s;
    struct strm_cap scap;
    pj_status_t status;

    PJ_ASSERT_RETURN(s && pval, PJ_EINVAL);

    scap.strm = strm;
    scap.cap = cap;
    scap.pval.cpval = pval;

    job_queue_post_job(strm->sf->jq, set_cap, &scap, 0, &status);

    return status;
}

/* API: Start stream. */
static pj_status_t sdl_stream_start(pjmedia_vid_dev_stream *strm)
{
    struct sdl_stream *stream = (struct sdl_stream*)strm;

    PJ_LOG(4, (THIS_FILE, "Starting sdl video stream"));

    stream->is_running = PJ_TRUE;

    return PJ_SUCCESS;
}


/* API: Stop stream. */
static pj_status_t sdl_stream_stop(pjmedia_vid_dev_stream *strm)
{
    struct sdl_stream *stream = (struct sdl_stream*)strm;

    PJ_LOG(4, (THIS_FILE, "Stopping sdl video stream"));

    stream->is_running = PJ_FALSE;

    return PJ_SUCCESS;
}


/* API: Destroy stream. */
static pj_status_t sdl_stream_destroy(pjmedia_vid_dev_stream *strm)
{
    struct sdl_stream *stream = (struct sdl_stream*)strm;
    pj_status_t status;

    PJ_ASSERT_RETURN(stream != NULL, PJ_EINVAL);

    sdl_stream_stop(strm);

    job_queue_post_job(stream->sf->jq, sdl_destroy_all, strm, 0, &status);
    if (status != PJ_SUCCESS)
        return status;

    pj_mutex_lock(stream->sf->mutex);
    if (!pj_list_empty(&stream->list_entry))
	pj_list_erase(&stream->list_entry);
    pj_mutex_unlock(stream->sf->mutex);

    pj_pool_release(stream->pool);

    return PJ_SUCCESS;
}

/****************************************************************************
 * Job queue implementation
 */
#if PJ_DARWINOS==0
static int job_thread(void * data)
{
    job_queue *jq = (job_queue *)data;

    while (1) {
        job *jb;

	/* Wait until there is a job. */
        pj_sem_wait(jq->sem);

        /* Make sure there is no pending jobs before we quit. */
        if (jq->is_quitting && jq->head == jq->tail && !jq->is_full)
            break;

        jb = jq->jobs[jq->head];
        jb->retval = (*jb->func)(jb->data);
        /* If job queue is full and we already finish all the pending
         * jobs, increase the size.
         */
        if (jq->is_full && ((jq->head + 1) % jq->size == jq->tail)) {
            unsigned i, head;
            pj_status_t status;

            if (jq->old_sem) {
                for (i = 0; i < jq->size / JOB_QUEUE_INC_FACTOR; i++) {
                    pj_sem_destroy(jq->old_sem[i]);
                }
            }
            jq->old_sem = jq->job_sem;

            /* Double the job queue size. */
            jq->size *= JOB_QUEUE_INC_FACTOR;
            pj_sem_destroy(jq->sem);
            status = pj_sem_create(jq->pool, "thread_sem", 0, jq->size + 1,
                                   &jq->sem);
            if (status != PJ_SUCCESS) {
                PJ_LOG(3, (THIS_FILE, "Failed growing SDL job queue size."));
                return 0;
            }
            jq->jobs = (job **)pj_pool_calloc(jq->pool, jq->size,
                                              sizeof(job *));
            jq->job_sem = (pj_sem_t **) pj_pool_calloc(jq->pool, jq->size,
                                                       sizeof(pj_sem_t *));
            for (i = 0; i < jq->size; i++) {
                status = pj_sem_create(jq->pool, "job_sem", 0, 1,
                                       &jq->job_sem[i]);
                if (status != PJ_SUCCESS) {
                    PJ_LOG(3, (THIS_FILE, "Failed growing SDL job "
                                          "queue size."));
                    return 0;
                }
            }
            jq->is_full = PJ_FALSE;
            head = jq->head;
            jq->head = jq->tail = 0;
            pj_sem_post(jq->old_sem[head]);
        } else {
            pj_sem_post(jq->job_sem[jq->head]);
            jq->head = (jq->head + 1) % jq->size;
        }
    }

    return 0;
}
#endif

static pj_status_t job_queue_create(pj_pool_t *pool, job_queue **pjq)
{
    unsigned i;
    pj_status_t status;

    job_queue *jq = PJ_POOL_ZALLOC_T(pool, job_queue);
    jq->pool = pool;
    jq->size = INITIAL_MAX_JOBS;
    status = pj_sem_create(pool, "thread_sem", 0, jq->size + 1, &jq->sem);
    if (status != PJ_SUCCESS)
        goto on_error;
    jq->jobs = (job **)pj_pool_calloc(pool, jq->size, sizeof(job *));
    jq->job_sem = (pj_sem_t **) pj_pool_calloc(pool, jq->size,
                                               sizeof(pj_sem_t *));
    for (i = 0; i < jq->size; i++) {
        status = pj_sem_create(pool, "job_sem", 0, 1, &jq->job_sem[i]);
        if (status != PJ_SUCCESS)
            goto on_error;
    }

    status = pj_mutex_create_recursive(pool, "job_mutex", &jq->mutex);
    if (status != PJ_SUCCESS)
        goto on_error;

#if defined(PJ_DARWINOS) && PJ_DARWINOS!=0
    PJ_UNUSED_ARG(status);
#else
    status = pj_thread_create(pool, "job_th", job_thread, jq, 0, 0,
                              &jq->thread);
    if (status != PJ_SUCCESS)
        goto on_error;
#endif /* PJ_DARWINOS */

    *pjq = jq;
    return PJ_SUCCESS;

on_error:
    job_queue_destroy(jq);
    return status;
}

static pj_status_t job_queue_post_job(job_queue *jq, job_func_ptr func,
				      void *data, unsigned flags,
				      pj_status_t *retval)
{
    job jb;
    int tail;

    if (jq->is_quitting)
        return PJ_EBUSY;

    jb.func = func;
    jb.data = data;
    jb.flags = flags;

#if defined(PJ_DARWINOS) && PJ_DARWINOS!=0
    PJ_UNUSED_ARG(tail);
    NSAutoreleasePool *apool = [[NSAutoreleasePool alloc]init];
    JQDelegate *jqd = [[JQDelegate alloc]init];
    jqd->pjob = &jb;
    [jqd performSelectorOnMainThread:@selector(run_job)
 	 withObject:nil waitUntilDone:YES];
    [jqd release];
    [apool release];
#else /* PJ_DARWINOS */
    pj_mutex_lock(jq->mutex);
    jq->jobs[jq->tail] = &jb;
    tail = jq->tail;
    jq->tail = (jq->tail + 1) % jq->size;
    if (jq->tail == jq->head) {
	jq->is_full = PJ_TRUE;
        PJ_LOG(4, (THIS_FILE, "SDL job queue is full, increasing "
                              "the queue size."));
        pj_sem_post(jq->sem);
        /* Wait until our posted job is completed. */
        pj_sem_wait(jq->job_sem[tail]);
        pj_mutex_unlock(jq->mutex);
    } else {
        pj_mutex_unlock(jq->mutex);
        pj_sem_post(jq->sem);
        /* Wait until our posted job is completed. */
        pj_sem_wait(jq->job_sem[tail]);
    }
#endif /* PJ_DARWINOS */

    *retval = jb.retval;

    return PJ_SUCCESS;
}

static pj_status_t job_queue_destroy(job_queue *jq)
{
    unsigned i;

    jq->is_quitting = PJ_TRUE;

    if (jq->thread) {
        pj_sem_post(jq->sem);
        pj_thread_join(jq->thread);
        pj_thread_destroy(jq->thread);
    }

    if (jq->sem) {
        pj_sem_destroy(jq->sem);
        jq->sem = NULL;
    }
    for (i = 0; i < jq->size; i++) {
        if (jq->job_sem[i]) {
            pj_sem_destroy(jq->job_sem[i]);
            jq->job_sem[i] = NULL;
        }
    }
    if (jq->old_sem) {
        for (i = 0; i < jq->size / JOB_QUEUE_INC_FACTOR; i++) {
            if (jq->old_sem[i]) {
                pj_sem_destroy(jq->old_sem[i]);
                jq->old_sem[i] = NULL;
            }
        }
    }
    if (jq->mutex) {
        pj_mutex_destroy(jq->mutex);
        jq->mutex = NULL;
    }

    return PJ_SUCCESS;
}

#ifdef _MSC_VER
#   pragma comment( lib, "sdl.lib")
#   if PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL
#	pragma comment(lib, "OpenGL32.lib")
#   endif /* PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL */
#endif /* _MSC_VER */


#endif	/* PJMEDIA_VIDEO_DEV_HAS_SDL */
