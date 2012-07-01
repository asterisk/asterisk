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
#include <pj/unicode.h>


#if defined(PJMEDIA_VIDEO_DEV_HAS_DSHOW) && PJMEDIA_VIDEO_DEV_HAS_DSHOW != 0


#ifdef _MSC_VER
#   pragma warning(push, 3)
#endif

#include <windows.h>
#define COBJMACROS
#include <DShow.h>

#ifdef _MSC_VER
#   pragma warning(pop)
#endif

#pragma comment(lib, "Strmiids.lib")
#pragma comment(lib, "Rpcrt4.lib")
#pragma comment(lib, "Quartz.lib")

#define THIS_FILE		"dshow_dev.c"
#define DEFAULT_CLOCK_RATE	90000
#define DEFAULT_WIDTH		640
#define DEFAULT_HEIGHT		480
#define DEFAULT_FPS		25

/* Temporarily disable DirectShow renderer (VMR) */
#define HAS_VMR			0

typedef void (*input_callback)(void *user_data, IMediaSample *pMediaSample);
typedef struct NullRenderer NullRenderer;
IBaseFilter* NullRenderer_Create(input_callback input_cb,
                                 void *user_data);
typedef struct SourceFilter SourceFilter;
IBaseFilter* SourceFilter_Create(SourceFilter **pSrc);
HRESULT SourceFilter_Deliver(SourceFilter *src, void *buf, long size);
void SourceFilter_SetMediaType(SourceFilter *src, AM_MEDIA_TYPE *pmt);

typedef struct dshow_fmt_info
{
    pjmedia_format_id    pjmedia_format;
    const GUID          *dshow_format;
} dshow_fmt_info;

static dshow_fmt_info dshow_fmts[] =
{
    {PJMEDIA_FORMAT_YUY2, &MEDIASUBTYPE_YUY2} ,
    {PJMEDIA_FORMAT_RGB24, &MEDIASUBTYPE_RGB24} ,
    {PJMEDIA_FORMAT_RGB32, &MEDIASUBTYPE_RGB32} ,
    {PJMEDIA_FORMAT_IYUV, &MEDIASUBTYPE_IYUV} ,
};

/* dshow_ device info */
struct dshow_dev_info
{
    pjmedia_vid_dev_info	 info;
    unsigned			 dev_id;
    WCHAR                        display_name[192];
};

/* dshow_ factory */
struct dshow_factory
{
    pjmedia_vid_dev_factory	 base;
    pj_pool_t			*pool;
    pj_pool_t			*dev_pool;
    pj_pool_factory		*pf;

    unsigned			 dev_count;
    struct dshow_dev_info	*dev_info;
};

/* Video stream. */
struct dshow_stream
{
    pjmedia_vid_dev_stream   base;		    /**< Base stream	    */
    pjmedia_vid_dev_param    param;		    /**< Settings	    */
    pj_pool_t		    *pool;		    /**< Memory pool.	    */

    pjmedia_vid_dev_cb	     vid_cb;		    /**< Stream callback.   */
    void		    *user_data;		    /**< Application data.  */

    pj_bool_t		     quit_flag;
    pj_bool_t		     rend_thread_exited;
    pj_bool_t		     cap_thread_exited;
    pj_bool_t		     cap_thread_initialized;
    pj_thread_desc	     cap_thread_desc;
    pj_thread_t		    *cap_thread;
    void                    *frm_buf;
    unsigned                 frm_buf_size;

    struct dshow_graph
    {
        IFilterGraph        *filter_graph;
        IMediaFilter        *media_filter;
        SourceFilter        *csource_filter;
        IBaseFilter         *source_filter;
        IBaseFilter         *rend_filter;
        AM_MEDIA_TYPE       *mediatype;
    } dgraph;

    pj_timestamp	     cap_ts;
    unsigned		     cap_ts_inc;
};


/* Prototypes */
static pj_status_t dshow_factory_init(pjmedia_vid_dev_factory *f);
static pj_status_t dshow_factory_destroy(pjmedia_vid_dev_factory *f);
static pj_status_t dshow_factory_refresh(pjmedia_vid_dev_factory *f);
static unsigned    dshow_factory_get_dev_count(pjmedia_vid_dev_factory *f);
static pj_status_t dshow_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					      unsigned index,
					      pjmedia_vid_dev_info *info);
static pj_status_t dshow_factory_default_param(pj_pool_t *pool,
                                               pjmedia_vid_dev_factory *f,
					       unsigned index,
					       pjmedia_vid_dev_param *param);
static pj_status_t dshow_factory_create_stream(
					pjmedia_vid_dev_factory *f,
					pjmedia_vid_dev_param *param,
					const pjmedia_vid_dev_cb *cb,
					void *user_data,
					pjmedia_vid_dev_stream **p_vid_strm);

static pj_status_t dshow_stream_get_param(pjmedia_vid_dev_stream *strm,
					  pjmedia_vid_dev_param *param);
static pj_status_t dshow_stream_get_cap(pjmedia_vid_dev_stream *strm,
				        pjmedia_vid_dev_cap cap,
				        void *value);
static pj_status_t dshow_stream_set_cap(pjmedia_vid_dev_stream *strm,
				        pjmedia_vid_dev_cap cap,
				        const void *value);
static pj_status_t dshow_stream_start(pjmedia_vid_dev_stream *strm);
static pj_status_t dshow_stream_put_frame(pjmedia_vid_dev_stream *strm,
                                          const pjmedia_frame *frame);
static pj_status_t dshow_stream_stop(pjmedia_vid_dev_stream *strm);
static pj_status_t dshow_stream_destroy(pjmedia_vid_dev_stream *strm);

/* Operations */
static pjmedia_vid_dev_factory_op factory_op =
{
    &dshow_factory_init,
    &dshow_factory_destroy,
    &dshow_factory_get_dev_count,
    &dshow_factory_get_dev_info,
    &dshow_factory_default_param,
    &dshow_factory_create_stream,
    &dshow_factory_refresh
};

static pjmedia_vid_dev_stream_op stream_op =
{
    &dshow_stream_get_param,
    &dshow_stream_get_cap,
    &dshow_stream_set_cap,
    &dshow_stream_start,
    NULL,
    &dshow_stream_put_frame,
    &dshow_stream_stop,
    &dshow_stream_destroy
};


/****************************************************************************
 * Factory operations
 */
/*
 * Init dshow_ video driver.
 */
pjmedia_vid_dev_factory* pjmedia_dshow_factory(pj_pool_factory *pf)
{
    struct dshow_factory *f;
    pj_pool_t *pool;

    pool = pj_pool_create(pf, "dshow video", 1000, 1000, NULL);
    f = PJ_POOL_ZALLOC_T(pool, struct dshow_factory);
    f->pf = pf;
    f->pool = pool;
    f->base.op = &factory_op;

    return &f->base;
}

/* API: init factory */
static pj_status_t dshow_factory_init(pjmedia_vid_dev_factory *f)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        PJ_LOG(4,(THIS_FILE, "Failed initializing DShow: "
                             "COM library already initialized with "
                             "incompatible concurrency model"));
        return PJMEDIA_EVID_INIT;
    }

    return dshow_factory_refresh(f);
}

/* API: destroy factory */
static pj_status_t dshow_factory_destroy(pjmedia_vid_dev_factory *f)
{
    struct dshow_factory *df = (struct dshow_factory*)f;
    pj_pool_t *pool = df->pool;

    df->pool = NULL;
    if (df->dev_pool)
        pj_pool_release(df->dev_pool);
    if (pool)
        pj_pool_release(pool);

    CoUninitialize();

    return PJ_SUCCESS;
}

static HRESULT get_cap_device(struct dshow_factory *df,
			      unsigned id,
			      IBaseFilter **filter)
{
    IBindCtx *pbc;
    HRESULT hr;

    hr = CreateBindCtx(0, &pbc);
    if (SUCCEEDED (hr)) {
	IMoniker *moniker;
	DWORD pchEaten;

	hr = MkParseDisplayName(pbc, df->dev_info[id].display_name,
				&pchEaten, &moniker);
	if (SUCCEEDED(hr)) {
	    hr = IMoniker_BindToObject(moniker, pbc, NULL,
				       &IID_IBaseFilter,
				       (LPVOID *)filter);
	    IMoniker_Release(moniker);
	}
	IBindCtx_Release(pbc);
    }

    return hr;
}

static void enum_dev_cap(IBaseFilter *filter,
			 pjmedia_dir dir,
			 const GUID *dshow_fmt,
			 AM_MEDIA_TYPE **pMediatype,
			 IPin **pSrcpin,
			 pj_bool_t *sup_fmt)
{
    IEnumPins *pEnum;
    AM_MEDIA_TYPE *mediatype = NULL;
    HRESULT hr;

    if (pSrcpin)
	*pSrcpin = NULL;
    hr = IBaseFilter_EnumPins(filter, &pEnum);
    if (SUCCEEDED(hr)) {
        /* Loop through all the pins. */
	IPin *pPin = NULL;

        while (IEnumPins_Next(pEnum, 1, &pPin, NULL) == S_OK) {
            PIN_DIRECTION pindirtmp;

            hr = IPin_QueryDirection(pPin, &pindirtmp);
            if (hr != S_OK || pindirtmp != PINDIR_OUTPUT) {
                if (SUCCEEDED(hr))
                    IPin_Release(pPin);
                continue;
            }

            if (dir == PJMEDIA_DIR_CAPTURE) {
                IAMStreamConfig *streamcaps;

                hr = IPin_QueryInterface(pPin, &IID_IAMStreamConfig,
                                         (LPVOID *)&streamcaps);
                if (SUCCEEDED(hr)) {
                    VIDEO_STREAM_CONFIG_CAPS vscc;
                    int i, isize, icount;

                    IAMStreamConfig_GetNumberOfCapabilities(streamcaps,
                                                            &icount, &isize);

                    for (i = 0; i < icount; i++) {
			unsigned j, nformat;
                        RPC_STATUS rpcstatus, rpcstatus2;

                        hr = IAMStreamConfig_GetStreamCaps(streamcaps, i,
                                                           &mediatype,
                                                           (BYTE *)&vscc);
                        if (FAILED (hr))
                            continue;

			nformat = (dshow_fmt? 1:
				   sizeof(dshow_fmts)/sizeof(dshow_fmts[0]));
			for (j = 0; j < nformat; j++) {
			    const GUID *dshow_format = dshow_fmt;
                            
			    if (!dshow_format)
				dshow_format = dshow_fmts[j].dshow_format;
			    if (UuidCompare(&mediatype->subtype, 
					    (UUID*)dshow_format,
					    &rpcstatus) == 0 && 
				rpcstatus == RPC_S_OK &&
				UuidCompare(&mediatype->formattype,
					    (UUID*)&FORMAT_VideoInfo,
					    &rpcstatus2) == 0 &&
				rpcstatus2 == RPC_S_OK)
			    {
				if (sup_fmt)
				    sup_fmt[j] = PJ_TRUE;
				if (pSrcpin) {
				    *pSrcpin = pPin;
				    *pMediatype = mediatype;
				}
			    }
			}
			if (pSrcpin && *pSrcpin)
			    break;
                    }
                    IAMStreamConfig_Release(streamcaps);
                }
            } else {
                *pSrcpin = pPin;
            }
            if (pSrcpin && *pSrcpin)
                break;
            IPin_Release(pPin);
	}
        IEnumPins_Release(pEnum);
    }
}

/* API: refresh the list of devices */
static pj_status_t dshow_factory_refresh(pjmedia_vid_dev_factory *f)
{
    struct dshow_factory *df = (struct dshow_factory*)f;
    struct dshow_dev_info *ddi;
    int dev_count = 0;
    unsigned c;
    ICreateDevEnum *dev_enum = NULL;
    IEnumMoniker *enum_cat = NULL;
    IMoniker *moniker = NULL;
    HRESULT hr;
    ULONG fetched;

    if (df->dev_pool) {
        pj_pool_release(df->dev_pool);
        df->dev_pool = NULL;
    }

    df->dev_count = 0;
    df->dev_pool = pj_pool_create(df->pf, "dshow video", 500, 500, NULL);

    hr = CoCreateInstance(&CLSID_SystemDeviceEnum, NULL,
                          CLSCTX_INPROC_SERVER, &IID_ICreateDevEnum,
                          (void**)&dev_enum);
    if (FAILED(hr) ||
        ICreateDevEnum_CreateClassEnumerator(dev_enum,
            &CLSID_VideoInputDeviceCategory, &enum_cat, 0) != S_OK) 
    {
	PJ_LOG(4,(THIS_FILE, "Windows found no video input devices"));
        if (dev_enum)
            ICreateDevEnum_Release(dev_enum);
	dev_count = 0;
    } else {
        while (IEnumMoniker_Next(enum_cat, 1, &moniker, &fetched) == S_OK) {
            dev_count++;
        }
    }

    /* Add renderer device */
    dev_count += 1;
    df->dev_info = (struct dshow_dev_info*)
 		   pj_pool_calloc(df->dev_pool, dev_count,
 				  sizeof(struct dshow_dev_info));

    if (dev_count > 1) {
        IEnumMoniker_Reset(enum_cat);
        while (IEnumMoniker_Next(enum_cat, 1, &moniker, &fetched) == S_OK) {
            IPropertyBag *prop_bag;

            hr = IMoniker_BindToStorage(moniker, 0, 0, &IID_IPropertyBag,
                                        (void**)&prop_bag);
            if (SUCCEEDED(hr)) {
                VARIANT var_name;

                VariantInit(&var_name);
                hr = IPropertyBag_Read(prop_bag, L"FriendlyName",
                                       &var_name, NULL);
                if (SUCCEEDED(hr) && var_name.bstrVal) {
                    WCHAR *wszDisplayName = NULL;
		    IBaseFilter *filter;

                    ddi = &df->dev_info[df->dev_count++];
                    pj_bzero(ddi, sizeof(*ddi));
                    pj_unicode_to_ansi(var_name.bstrVal,
                                       wcslen(var_name.bstrVal),
                                       ddi->info.name,
                                       sizeof(ddi->info.name));

                    hr = IMoniker_GetDisplayName(moniker, NULL, NULL,
                                                 &wszDisplayName);
                    if (hr == S_OK && wszDisplayName) {
                        pj_memcpy(ddi->display_name, wszDisplayName,
                                  (wcslen(wszDisplayName)+1) * sizeof(WCHAR));
                        CoTaskMemFree(wszDisplayName);
                    }

                    strncpy(ddi->info.driver, "dshow", 
                            sizeof(ddi->info.driver));
                    ddi->info.driver[sizeof(ddi->info.driver)-1] = '\0';
                    ddi->info.dir = PJMEDIA_DIR_CAPTURE;
                    ddi->info.has_callback = PJ_TRUE;

                    /* Set the device capabilities here */
                    ddi->info.caps = PJMEDIA_VID_DEV_CAP_FORMAT;

		    hr = get_cap_device(df, df->dev_count-1, &filter);
		    if (SUCCEEDED(hr)) {
			unsigned j;
			pj_bool_t sup_fmt[sizeof(dshow_fmts)/sizeof(dshow_fmts[0])];

			pj_bzero(sup_fmt, sizeof(sup_fmt));
			enum_dev_cap(filter, ddi->info.dir, NULL, NULL, NULL, sup_fmt);

			ddi->info.fmt_cnt = 0;
			for (j = 0;
			     j < sizeof(dshow_fmts)/sizeof(dshow_fmts[0]);
			     j++)
			{
			    if (!sup_fmt[j])
				continue;
			    pjmedia_format_init_video(
				&ddi->info.fmt[ddi->info.fmt_cnt++],
				dshow_fmts[j].pjmedia_format, 
				DEFAULT_WIDTH, DEFAULT_HEIGHT, 
				DEFAULT_FPS, 1);
			}
		    }
                }
                VariantClear(&var_name);

                IPropertyBag_Release(prop_bag);
            }
            IMoniker_Release(moniker);
        }

        IEnumMoniker_Release(enum_cat);
        ICreateDevEnum_Release(dev_enum);
    }

#if HAS_VMR
    ddi = &df->dev_info[df->dev_count++];
    pj_bzero(ddi, sizeof(*ddi));
    pj_ansi_strncpy(ddi->info.name,  "Video Mixing Renderer",
                    sizeof(ddi->info.name));
    ddi->info.name[sizeof(ddi->info.name)-1] = '\0';
    pj_ansi_strncpy(ddi->info.driver, "dshow", sizeof(ddi->info.driver));
    ddi->info.driver[sizeof(ddi->info.driver)-1] = '\0';
    ddi->info.dir = PJMEDIA_DIR_RENDER;
    ddi->info.has_callback = PJ_FALSE;
    ddi->info.caps = PJMEDIA_VID_DEV_CAP_FORMAT;
//    TODO:
//    ddi->info.caps |= PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW;

    ddi->info.fmt_cnt = 1;
    pjmedia_format_init_video(&ddi->info.fmt[0], dshow_fmts[0].pjmedia_format, 
			      DEFAULT_WIDTH, DEFAULT_HEIGHT, 
			      DEFAULT_FPS, 1);
#endif

    PJ_LOG(4, (THIS_FILE, "DShow has %d devices:", 
	       df->dev_count));
    for (c = 0; c < df->dev_count; ++c) {
	PJ_LOG(4, (THIS_FILE, " dev_id %d: %s (%s)", 
	       c,
	       df->dev_info[c].info.name,
	       df->dev_info[c].info.dir & PJMEDIA_DIR_CAPTURE ?
               "capture" : "render"));
    }

    return PJ_SUCCESS;
}

/* API: get number of devices */
static unsigned dshow_factory_get_dev_count(pjmedia_vid_dev_factory *f)
{
    struct dshow_factory *df = (struct dshow_factory*)f;
    return df->dev_count;
}

/* API: get device info */
static pj_status_t dshow_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					      unsigned index,
					      pjmedia_vid_dev_info *info)
{
    struct dshow_factory *df = (struct dshow_factory*)f;

    PJ_ASSERT_RETURN(index < df->dev_count, PJMEDIA_EVID_INVDEV);

    pj_memcpy(info, &df->dev_info[index].info, sizeof(*info));

    return PJ_SUCCESS;
}

/* API: create default device parameter */
static pj_status_t dshow_factory_default_param(pj_pool_t *pool,
                                               pjmedia_vid_dev_factory *f,
					       unsigned index,
					       pjmedia_vid_dev_param *param)
{
    struct dshow_factory *df = (struct dshow_factory*)f;
    struct dshow_dev_info *di = &df->dev_info[index];

    PJ_ASSERT_RETURN(index < df->dev_count, PJMEDIA_EVID_INVDEV);

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

    /* Set the device capabilities here */
    param->clock_rate = DEFAULT_CLOCK_RATE;
    param->flags = PJMEDIA_VID_DEV_CAP_FORMAT;

    pjmedia_format_copy(&param->fmt, &di->info.fmt[0]);

    return PJ_SUCCESS;
}

static void input_cb(void *user_data, IMediaSample *pMediaSample)
{
    struct dshow_stream *strm = (struct dshow_stream*)user_data;
    pjmedia_frame frame = {0};

    if (strm->quit_flag) {
        strm->cap_thread_exited = PJ_TRUE;
        return;
    }

    if (strm->cap_thread_initialized == 0 || !pj_thread_is_registered())
    {
        pj_status_t status;

	status = pj_thread_register("ds_cap", strm->cap_thread_desc, 
				    &strm->cap_thread);
        if (status != PJ_SUCCESS)
            return;
	strm->cap_thread_initialized = 1;
	PJ_LOG(5,(THIS_FILE, "Capture thread started"));
    }

    frame.type = PJMEDIA_FRAME_TYPE_VIDEO;
    IMediaSample_GetPointer(pMediaSample, (BYTE **)&frame.buf);
    frame.size = IMediaSample_GetActualDataLength(pMediaSample);
    frame.bit_info = 0;
    frame.timestamp = strm->cap_ts;
    strm->cap_ts.u64 += strm->cap_ts_inc;

    if (strm->frm_buf_size) {
        unsigned i, stride;
        BYTE *src_buf, *dst_buf;
        pjmedia_video_format_detail *vfd;
        
        /* Image is bottom-up, convert it to top-down. */
        src_buf = dst_buf = (BYTE *)frame.buf;
        stride = strm->frm_buf_size;
        vfd = pjmedia_format_get_video_format_detail(&strm->param.fmt,
                                                     PJ_TRUE);
        src_buf += (vfd->size.h - 1) * stride;

        for (i = vfd->size.h / 2; i > 0; i--) {
            memcpy(strm->frm_buf, dst_buf, stride);
            memcpy(dst_buf, src_buf, stride);
            memcpy(src_buf, strm->frm_buf, stride);
            dst_buf += stride;
            src_buf -= stride;
        }
    }

    if (strm->vid_cb.capture_cb)
        (*strm->vid_cb.capture_cb)(&strm->base, strm->user_data, &frame);
}

/* API: Put frame from stream */
static pj_status_t dshow_stream_put_frame(pjmedia_vid_dev_stream *strm,
                                          const pjmedia_frame *frame)
{
    struct dshow_stream *stream = (struct dshow_stream*)strm;
    HRESULT hr;

    if (stream->quit_flag) {
        stream->rend_thread_exited = PJ_TRUE;
        return PJ_SUCCESS;
    }

    hr = SourceFilter_Deliver(stream->dgraph.csource_filter,
                              frame->buf, frame->size);
    if (FAILED(hr))
        return hr;

    return PJ_SUCCESS;
}

static dshow_fmt_info* get_dshow_format_info(pjmedia_format_id id)
{
    unsigned i;

    for (i = 0; i < sizeof(dshow_fmts)/sizeof(dshow_fmts[0]); i++) {
        if (dshow_fmts[i].pjmedia_format == id)
            return &dshow_fmts[i];
    }

    return NULL;
}

static pj_status_t create_filter_graph(pjmedia_dir dir,
                                       unsigned id,
                                       pj_bool_t use_def_size,
                                       pj_bool_t use_def_fps,
                                       struct dshow_factory *df,
                                       struct dshow_stream *strm,
                                       struct dshow_graph *graph)
{
    HRESULT hr;
    IEnumPins *pEnum;
    IPin *srcpin = NULL;
    IPin *sinkpin = NULL;
    AM_MEDIA_TYPE *mediatype= NULL, mtype;
    VIDEOINFOHEADER *video_info, *vi = NULL;
    pjmedia_video_format_detail *vfd;
    const pjmedia_video_format_info *vfi;

    vfi = pjmedia_get_video_format_info(pjmedia_video_format_mgr_instance(),
                                        strm->param.fmt.id);
    if (!vfi)
        return PJMEDIA_EVID_BADFORMAT;

    hr = CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC,
                          &IID_IFilterGraph, (LPVOID *)&graph->filter_graph);
    if (FAILED(hr)) {
        goto on_error;
    }

    hr = IFilterGraph_QueryInterface(graph->filter_graph, &IID_IMediaFilter,
                                     (LPVOID *)&graph->media_filter);
    if (FAILED(hr)) {
        goto on_error;
    }

    if (dir == PJMEDIA_DIR_CAPTURE) {
	hr = get_cap_device(df, id, &graph->source_filter);
	if (FAILED(hr)) {
            goto on_error;
        }
    } else {
        graph->source_filter = SourceFilter_Create(&graph->csource_filter);
    }

    hr = IFilterGraph_AddFilter(graph->filter_graph, graph->source_filter,
                                L"capture");
    if (FAILED(hr)) {
        goto on_error;
    }

    if (dir == PJMEDIA_DIR_CAPTURE) {
        graph->rend_filter = NullRenderer_Create(input_cb, strm);
    } else {
        hr = CoCreateInstance(&CLSID_VideoMixingRenderer, NULL,
                              CLSCTX_INPROC, &IID_IBaseFilter,
                              (LPVOID *)&graph->rend_filter);
        if (FAILED (hr)) {
            goto on_error;
        }
    }

    IBaseFilter_EnumPins(graph->rend_filter, &pEnum);
    if (SUCCEEDED(hr)) {
        // Loop through all the pins
	IPin *pPin = NULL;

        while (IEnumPins_Next(pEnum, 1, &pPin, NULL) == S_OK) {
            PIN_DIRECTION pindirtmp;

            hr = IPin_QueryDirection(pPin, &pindirtmp);
            if (hr == S_OK && pindirtmp == PINDIR_INPUT) {
                sinkpin = pPin;
                break;
            }
	    IPin_Release(pPin);
        }
        IEnumPins_Release(pEnum);
    }

    vfd = pjmedia_format_get_video_format_detail(&strm->param.fmt, PJ_TRUE);

    enum_dev_cap(graph->source_filter, dir,
		 get_dshow_format_info(strm->param.fmt.id)->dshow_format,
		 &mediatype, &srcpin, NULL);
    graph->mediatype = mediatype;

    if (srcpin && dir == PJMEDIA_DIR_RENDER) {
	mediatype = graph->mediatype = &mtype;

	memset (mediatype, 0, sizeof(AM_MEDIA_TYPE));
	mediatype->majortype = MEDIATYPE_Video;
	mediatype->subtype = *(get_dshow_format_info(strm->param.fmt.id)->
			       dshow_format);
	mediatype->bFixedSizeSamples = TRUE;
	mediatype->bTemporalCompression = FALSE;

	vi = (VIDEOINFOHEADER *)
	    CoTaskMemAlloc(sizeof(VIDEOINFOHEADER));
	memset (vi, 0, sizeof(VIDEOINFOHEADER));
	mediatype->formattype = FORMAT_VideoInfo;
	mediatype->cbFormat = sizeof(VIDEOINFOHEADER);
	mediatype->pbFormat = (BYTE *)vi;

	vi->rcSource.bottom = vfd->size.h;
	vi->rcSource.right = vfd->size.w;
	vi->rcTarget.bottom = vfd->size.h;
	vi->rcTarget.right = vfd->size.w;

	vi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	vi->bmiHeader.biPlanes = 1;
	vi->bmiHeader.biBitCount = vfi->bpp;
	vi->bmiHeader.biCompression = strm->param.fmt.id;
    }

    if (!srcpin || !sinkpin || !mediatype) {
        hr = VFW_E_TYPE_NOT_ACCEPTED;
        goto on_error;
    }
    video_info = (VIDEOINFOHEADER *) mediatype->pbFormat;
    if (!use_def_size) {
        video_info->bmiHeader.biWidth = vfd->size.w;
        video_info->bmiHeader.biHeight = vfd->size.h;
    }
    if (video_info->AvgTimePerFrame == 0 ||
        (!use_def_fps && vfd->fps.num != 0))
    {
        video_info->AvgTimePerFrame = (LONGLONG) (10000000 * 
						  (double)vfd->fps.denum /
						  vfd->fps.num);
    }
    video_info->bmiHeader.biSizeImage = DIBSIZE(video_info->bmiHeader);
    mediatype->lSampleSize = DIBSIZE(video_info->bmiHeader);
    if (graph->csource_filter)
        SourceFilter_SetMediaType(graph->csource_filter,
                                  mediatype);

    hr = IFilterGraph_AddFilter(graph->filter_graph,
                                (IBaseFilter *)graph->rend_filter,
                                L"renderer");
    if (FAILED(hr))
        goto on_error;

    hr = IFilterGraph_ConnectDirect(graph->filter_graph, srcpin, sinkpin,
                                    mediatype);
    if (SUCCEEDED(hr)) {
        if (use_def_size || use_def_fps) {
            pjmedia_format_init_video(&strm->param.fmt, strm->param.fmt.id,
                                      video_info->bmiHeader.biWidth,
                                      video_info->bmiHeader.biHeight,
                                      10000000,
                                      (unsigned)video_info->AvgTimePerFrame);
        }

        strm->frm_buf_size = 0;
        if (dir == PJMEDIA_DIR_CAPTURE &&
            video_info->bmiHeader.biCompression == BI_RGB &&
            video_info->bmiHeader.biHeight > 0)
        {
            /* Allocate buffer to flip the captured image. */
            strm->frm_buf_size = (video_info->bmiHeader.biBitCount >> 3) *
                                 video_info->bmiHeader.biWidth;
            strm->frm_buf = pj_pool_alloc(strm->pool, strm->frm_buf_size);
        }
    }

on_error:
    if (srcpin)
        IPin_Release(srcpin);
    if (sinkpin)
        IPin_Release(sinkpin);
    if (vi)
        CoTaskMemFree(vi);
    if (FAILED(hr)) {
	char msg[80];
	if (AMGetErrorText(hr, msg, sizeof(msg))) {
	    PJ_LOG(4,(THIS_FILE, "Error creating filter graph: %s (hr=0x%x)", 
		      msg, hr));
	}
        return PJ_EUNKNOWN;
    }

    return PJ_SUCCESS;
}

static void destroy_filter_graph(struct dshow_stream * stream)
{
    if (stream->dgraph.source_filter) {
        IBaseFilter_Release(stream->dgraph.source_filter);
        stream->dgraph.source_filter = NULL;
    }
    if (stream->dgraph.rend_filter) {
        IBaseFilter_Release(stream->dgraph.rend_filter);
        stream->dgraph.rend_filter = NULL;
    }
    if (stream->dgraph.media_filter) {
        IMediaFilter_Release(stream->dgraph.media_filter);
        stream->dgraph.media_filter = NULL;
    }
    if (stream->dgraph.filter_graph) {
        IFilterGraph_Release(stream->dgraph.filter_graph);
        stream->dgraph.filter_graph = NULL;
    }
}

/* API: create stream */
static pj_status_t dshow_factory_create_stream(
					pjmedia_vid_dev_factory *f,
					pjmedia_vid_dev_param *param,
					const pjmedia_vid_dev_cb *cb,
					void *user_data,
					pjmedia_vid_dev_stream **p_vid_strm)
{
    struct dshow_factory *df = (struct dshow_factory*)f;
    pj_pool_t *pool;
    struct dshow_stream *strm;
    pj_status_t status;

    PJ_ASSERT_RETURN(param->dir == PJMEDIA_DIR_CAPTURE ||
                     param->dir == PJMEDIA_DIR_RENDER, PJ_EINVAL);

    if (!get_dshow_format_info(param->fmt.id))
        return PJMEDIA_EVID_BADFORMAT;

    /* Create and Initialize stream descriptor */
    pool = pj_pool_create(df->pf, "dshow-dev", 1000, 1000, NULL);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    strm = PJ_POOL_ZALLOC_T(pool, struct dshow_stream);
    pj_memcpy(&strm->param, param, sizeof(*param));
    strm->pool = pool;
    pj_memcpy(&strm->vid_cb, cb, sizeof(*cb));
    strm->user_data = user_data;

    if (param->dir & PJMEDIA_DIR_CAPTURE) {
	const pjmedia_video_format_detail *vfd;

        /* Create capture stream here */
        status = create_filter_graph(PJMEDIA_DIR_CAPTURE, param->cap_id,
                                     PJ_FALSE, PJ_FALSE, df, strm,
                                     &strm->dgraph);
        if (status != PJ_SUCCESS) {
            destroy_filter_graph(strm);
            /* Try to use default fps */
            PJ_LOG(4,(THIS_FILE, "Trying to open dshow dev with default fps"));
            status = create_filter_graph(PJMEDIA_DIR_CAPTURE, param->cap_id,
                                         PJ_FALSE, PJ_TRUE, df, strm,
                                         &strm->dgraph);

            if (status != PJ_SUCCESS) {
                /* Still failed, now try to use default fps and size */
                destroy_filter_graph(strm);
                /* Try to use default fps */
                PJ_LOG(4,(THIS_FILE, "Trying to open dshow dev with default "
                                     "size & fps"));
                status = create_filter_graph(PJMEDIA_DIR_CAPTURE,
                                             param->cap_id,
                                             PJ_TRUE, PJ_TRUE, df, strm,
                                             &strm->dgraph);
            }

            if (status != PJ_SUCCESS)
                goto on_error;
            pj_memcpy(param, &strm->param, sizeof(*param));
        }
	
	vfd = pjmedia_format_get_video_format_detail(&param->fmt, PJ_TRUE);
	strm->cap_ts_inc = PJMEDIA_SPF2(param->clock_rate, &vfd->fps, 1);
    } else if (param->dir & PJMEDIA_DIR_RENDER) {
        /* Create render stream here */
        status = create_filter_graph(PJMEDIA_DIR_RENDER, param->rend_id,
                                     PJ_FALSE, PJ_FALSE, df, strm,
                                     &strm->dgraph);
        if (status != PJ_SUCCESS)
            goto on_error;
    }

    /* Apply the remaining settings */
    if (param->flags & PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW) {
	dshow_stream_set_cap(&strm->base,
		            PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW,
		            &param->window);
    }

    /* Done */
    strm->base.op = &stream_op;
    *p_vid_strm = &strm->base;

    return PJ_SUCCESS;
 
on_error:
    dshow_stream_destroy((pjmedia_vid_dev_stream *)strm);
    return status;
}

/* API: Get stream info. */
static pj_status_t dshow_stream_get_param(pjmedia_vid_dev_stream *s,
					  pjmedia_vid_dev_param *pi)
{
    struct dshow_stream *strm = (struct dshow_stream*)s;

    PJ_ASSERT_RETURN(strm && pi, PJ_EINVAL);

    pj_memcpy(pi, &strm->param, sizeof(*pi));

    if (dshow_stream_get_cap(s, PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW,
			     &pi->window) == PJ_SUCCESS)
    {
        pi->flags |= PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW;
    }

    return PJ_SUCCESS;
}

/* API: get capability */
static pj_status_t dshow_stream_get_cap(pjmedia_vid_dev_stream *s,
				        pjmedia_vid_dev_cap cap,
				        void *pval)
{
    struct dshow_stream *strm = (struct dshow_stream*)s;

    PJ_UNUSED_ARG(strm);

    PJ_ASSERT_RETURN(s && pval, PJ_EINVAL);

    if (cap==PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW)
    {
	*(unsigned*)pval = 0;
	return PJ_SUCCESS;
    } else {
	return PJMEDIA_EVID_INVCAP;
    }
}

/* API: set capability */
static pj_status_t dshow_stream_set_cap(pjmedia_vid_dev_stream *s,
				        pjmedia_vid_dev_cap cap,
				        const void *pval)
{
    struct dshow_stream *strm = (struct dshow_stream*)s;

    PJ_UNUSED_ARG(strm);

    PJ_ASSERT_RETURN(s && pval, PJ_EINVAL);

    if (cap==PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW)
    {
	// set renderer's window here
	return PJ_SUCCESS;
    }

    return PJMEDIA_EVID_INVCAP;
}

/* API: Start stream. */
static pj_status_t dshow_stream_start(pjmedia_vid_dev_stream *strm)
{
    struct dshow_stream *stream = (struct dshow_stream*)strm;
    HRESULT hr;

    stream->quit_flag = PJ_FALSE;
    stream->cap_thread_exited = PJ_FALSE;
    stream->rend_thread_exited = PJ_FALSE;

    hr = IMediaFilter_Run(stream->dgraph.media_filter, 0);
    if (FAILED(hr)) {
        char msg[80];
        if (AMGetErrorText(hr, msg, sizeof(msg))) {
            PJ_LOG(4,(THIS_FILE, "Error starting media: %s", msg));
        }
        return PJ_EUNKNOWN;
    }

    PJ_LOG(4, (THIS_FILE, "Starting dshow video stream"));

    return PJ_SUCCESS;
}

/* API: Stop stream. */
static pj_status_t dshow_stream_stop(pjmedia_vid_dev_stream *strm)
{
    struct dshow_stream *stream = (struct dshow_stream*)strm;
    unsigned i;

    stream->quit_flag = PJ_TRUE;
    if (stream->cap_thread) {
        for (i=0; !stream->cap_thread_exited && i<100; ++i)
	    pj_thread_sleep(10);
    }
    for (i=0; !stream->rend_thread_exited && i<100; ++i)
	pj_thread_sleep(10);

    IMediaFilter_Stop(stream->dgraph.media_filter);

    PJ_LOG(4, (THIS_FILE, "Stopping dshow video stream"));

    return PJ_SUCCESS;
}

/* API: Destroy stream. */
static pj_status_t dshow_stream_destroy(pjmedia_vid_dev_stream *strm)
{
    struct dshow_stream *stream = (struct dshow_stream*)strm;

    PJ_ASSERT_RETURN(stream != NULL, PJ_EINVAL);

    dshow_stream_stop(strm);
    destroy_filter_graph(stream);

    pj_pool_release(stream->pool);

    return PJ_SUCCESS;
}

#endif	/* PJMEDIA_VIDEO_DEV_HAS_DSHOW */
