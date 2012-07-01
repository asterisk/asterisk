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
#ifndef __PJMEDIA_VIDEODEV_VIDEODEV_H__
#define __PJMEDIA_VIDEODEV_VIDEODEV_H__

/**
 * @file videodev.h
 * @brief Video device API.
 */
#include <pjmedia-videodev/config.h>
#include <pjmedia-videodev/errno.h>
#include <pjmedia/event.h>
#include <pjmedia/frame.h>
#include <pjmedia/format.h>
#include <pj/pool.h>


PJ_BEGIN_DECL

/**
 * @defgroup video_device_reference Video Device API Reference
 * @ingroup video_device_api
 * @brief API Reference
 * @{
 */
 
/**
 * Type for device index.
 */
typedef pj_int32_t pjmedia_vid_dev_index;

/**
 * Enumeration of window handle type.
 */
typedef enum pjmedia_vid_dev_hwnd_type
{
    /**
     * Type none.
     */
    PJMEDIA_VID_DEV_HWND_TYPE_NONE,

    /**
     * Native window handle on Windows.
     */
    PJMEDIA_VID_DEV_HWND_TYPE_WINDOWS

} pjmedia_vid_dev_hwnd_type;

/**
 * Type for window handle.
 */
typedef struct pjmedia_vid_dev_hwnd
{
    /**
     * The window handle type.
     */
    pjmedia_vid_dev_hwnd_type type;

    /**
     * The window handle.
     */
    union
    {
	struct {
	    void    *hwnd;	/**< HWND     	*/
	} win;
	struct {
	    void    *window;    /**< Window	*/
	    void    *display;   /**< Display	*/
	} x11;
	struct {
	    void    *window;    /**< Window	*/
	} cocoa;
	struct {
	    void    *window;    /**< Window	*/
	} ios;
	void 	    *window;
    } info;

} pjmedia_vid_dev_hwnd;

/**
 * Parameter for switching device with PJMEDIA_VID_DEV_CAP_SWITCH capability.
 * Initialize this with pjmedia_vid_dev_switch_param_default()
 */
typedef struct pjmedia_vid_dev_switch_param
{
    /**
     * Target device ID to switch to. Once the switching is successful, the
     * video stream will use this device and the old device will be closed.
     */
    pjmedia_vid_dev_index target_id;

} pjmedia_vid_dev_switch_param;


/**
 * Enumeration of window flags.
 */
typedef enum pjmedia_vid_dev_wnd_flag
{
    /**
     * Window with border.
     */
    PJMEDIA_VID_DEV_WND_BORDER = 1,

    /**
     * Window can be resized.
     */
    PJMEDIA_VID_DEV_WND_RESIZABLE = 2

} pjmedia_vid_dev_wnd_flag;


/**
 * Device index constants.
 */
enum
{
    /**
     * Constant to denote default capture device
     */
    PJMEDIA_VID_DEFAULT_CAPTURE_DEV = -1,

    /**
     * Constant to denote default render device
     */
    PJMEDIA_VID_DEFAULT_RENDER_DEV = -2,

    /**
     * Constant to denote invalid device index.
     */
    PJMEDIA_VID_INVALID_DEV = -3
};


/**
 * This enumeration identifies various video device capabilities. These video
 * capabilities indicates what features are supported by the underlying
 * video device implementation.
 *
 * Applications get these capabilities in the #pjmedia_vid_dev_info structure.
 *
 * Application can also set the specific features/capabilities when opening
 * the video stream by setting the \a flags member of #pjmedia_vid_dev_param
 * structure.
 *
 * Once video stream is running, application can also retrieve or set some
 * specific video capability, by using #pjmedia_vid_dev_stream_get_cap() and
 * #pjmedia_vid_dev_stream_set_cap() and specifying the desired capability. The
 * value of the capability is specified as pointer, and application needs to
 * supply the pointer with the correct value, according to the documentation
 * of each of the capability.
 */
typedef enum pjmedia_vid_dev_cap
{
    /**
     * Support for video formats. The value of this capability
     * is represented by #pjmedia_format structure.
     */
    PJMEDIA_VID_DEV_CAP_FORMAT = 1,

    /**
     * Support for video input scaling
     */
    PJMEDIA_VID_DEV_CAP_INPUT_SCALE = 2,

    /**
     * Support for returning the native window handle of the video window.
     * For renderer, this means the window handle of the renderer window,
     * while for capture, this means the window handle of the native preview,
     * only if the device supports  PJMEDIA_VID_DEV_CAP_INPUT_PREVIEW
     * capability.
     *
     * The value of this capability is pointer to pjmedia_vid_dev_hwnd
     * structure.
     */
    PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW = 4,

    /**
     * Support for resizing video output. This capability SHOULD be 
     * implemented by renderer, to alter the video output dimension on the fly.
     * Value is pjmedia_rect_size. 
     */
    PJMEDIA_VID_DEV_CAP_OUTPUT_RESIZE = 8,

    /**
     * Support for setting the video window's position.
     * Value is pjmedia_coord specifying the window's new coordinate.
     */
    PJMEDIA_VID_DEV_CAP_OUTPUT_POSITION = 16,

    /**
     * Support for setting the video output's visibility.
     * The value of this capability is a pj_bool_t containing boolean
     * PJ_TRUE or PJ_FALSE.
     */
    PJMEDIA_VID_DEV_CAP_OUTPUT_HIDE = 32,

    /**
     * Support for native preview capability in capture devices. Value is
     * pj_bool_t. With native preview, capture device can be instructed to
     * show or hide a preview window showing video directly from the camera
     * by setting this capability to PJ_TRUE or PJ_FALSE. Once the preview
     * is started, application may use PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW
     * capability to query the vidow window.
     *
     * The value of this capability is a pj_bool_t containing boolean
     * PJ_TRUE or PJ_FALSE.
     */
    PJMEDIA_VID_DEV_CAP_INPUT_PREVIEW = 64,

    /**
     * Support for changing video orientation in renderer and querying
     * video orientation info in capture. Changing video orientation in
     * a renderer will potentially affect the size of render window,
     * i.e: width and height swap. When a capture device supports this
     * capability, it will generate event PJMEDIA_EVENT_ORIENT_CHANGED
     * (see #pjmedia_event) everytime the capture orientation is changed.
     *
     * The value of this capability is pjmedia_orient.
     */
    PJMEDIA_VID_DEV_CAP_ORIENTATION = 128,

    /**
     * Support for fast switching to another device. A video stream with this
     * capability allows replacing of its underlying device with another
     * device, saving the user from opening a new video stream and gets a much
     * faster and smoother switching action.
     *
     * Note that even when this capability is supported by a device, it may
     * not be able to switch to arbitrary device. Application must always
     * check the return value of the operation to verify that switching has
     * occurred.
     *
     * This capability is currently write-only (i.e. set-only).
     *
     * The value of this capability is pointer to pjmedia_vid_dev_switch_param
     * structure.
     */
    PJMEDIA_VID_DEV_CAP_SWITCH = 256,

    /**
     * Support for setting the output video window's flags.
     * The value of this capability is a bitmask combination of
     * #pjmedia_vid_dev_wnd_flag.
     */
    PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW_FLAGS = 512,

    /**
     * End of standard capability
     */
    PJMEDIA_VID_DEV_CAP_MAX = 16384

} pjmedia_vid_dev_cap;

/**
 * Device information structure returned by #pjmedia_vid_dev_get_info().
 */
typedef struct pjmedia_vid_dev_info
{
    /** The device ID */
    pjmedia_vid_dev_index id;

    /** The device name */
    char name[64];

    /** The underlying driver name */
    char driver[32];

    /** 
     * The supported direction of the video device, i.e. whether it supports 
     * capture only, render only, or both.
     */
    pjmedia_dir dir;

    /**
     * Specify whether the device supports callback. Devices that implement
     * "active interface" will actively call the callbacks to give or ask for
     * video frames. If the device doesn't support callback, application
     * must actively request or give video frames from/to the device by using
     * pjmedia_vid_dev_stream_get_frame()/pjmedia_vid_dev_stream_put_frame().
     */
    pj_bool_t has_callback;

    /** Device capabilities, as bitmask combination of #pjmedia_vid_dev_cap */
    unsigned caps;

    /** Number of video formats supported by this device */
    unsigned fmt_cnt;

    /** 
     * Array of supported video formats. Some fields in each supported video
     * format may be set to zero or of "unknown" value, to indicate that the
     * value is unknown or should be ignored. When these value are not set
     * to zero, it indicates that the exact format combination is being used. 
     */
    pjmedia_format fmt[PJMEDIA_VID_DEV_INFO_FMT_CNT];

} pjmedia_vid_dev_info;


/** Forward declaration for pjmedia_vid_dev_stream */
typedef struct pjmedia_vid_dev_stream pjmedia_vid_dev_stream;

typedef struct pjmedia_vid_dev_cb
{
    /**
    * This callback is called by capturer stream when it has captured the
    * whole packet worth of video samples.
    *
    * @param stream	   The video stream.
    * @param user_data     User data associated with the stream.
    * @param frame         Captured frame.
    *
    * @return              Returning non-PJ_SUCCESS will cause the video
    *                      stream to stop
    */
    pj_status_t (*capture_cb)(pjmedia_vid_dev_stream *stream,
		              void *user_data,
                              pjmedia_frame *frame);

    /**
    * This callback is called by renderer stream when it needs additional
    * data to be rendered by the device. Application must fill in the whole
    * of output buffer with video samples.
    *
    * The frame argument contains the following values:
    *  - timestamp         Rendering timestamp, in samples.
    *  - buf               Buffer to be filled out by application.
    *  - size              The size requested in bytes, which will be equal
    *                      to the size of one whole packet.
    *
    * @param stream	   The video stream.
    * @param user_data     User data associated with the stream.
    * @param frame         Video frame, which buffer is to be filled in by
    *                      the application.
    *
    * @return              Returning non-PJ_SUCCESS will cause the video 
    *                      stream to stop
    */
    pj_status_t (*render_cb)(pjmedia_vid_dev_stream *stream,
			     void *user_data,
                             pjmedia_frame *frame);

} pjmedia_vid_dev_cb;


/**
 * This structure specifies the parameters to open the video stream.
 */
typedef struct pjmedia_vid_dev_param
{
    /**
     * The video direction. This setting is mandatory.
     */
    pjmedia_dir dir;

    /**
     * The video capture device ID. This setting is mandatory if the video
     * direction includes input/capture direction.
     */
    pjmedia_vid_dev_index cap_id;

    /**
     * The video render device ID. This setting is mandatory if the video
     * direction includes output/render direction.
     */
    pjmedia_vid_dev_index rend_id;

    /** 
     * Video clock rate. This setting is mandatory if the video
     * direction includes input/capture direction
     */
    unsigned clock_rate;

    /**
     * Video frame rate. This setting is mandatory if the video
     * direction includes input/capture direction
     */
//    pjmedia_ratio frame_rate;

    /**
     * This flags specifies which of the optional settings are valid in this
     * structure. The flags is bitmask combination of pjmedia_vid_dev_cap.
     */
    unsigned flags;

    /**
     * Set the video format. This setting is mandatory.
     */
    pjmedia_format fmt;

    /**
     * Window for the renderer to display the video. This setting is optional,
     * and will only be used if PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW is set in 
     * the flags.
     */
    pjmedia_vid_dev_hwnd window;

    /**
     * Video display size. This setting is optional, and will only be used 
     * if PJMEDIA_VID_DEV_CAP_OUTPUT_RESIZE is set in the flags.
     */
    pjmedia_rect_size disp_size;

    /**
     * Video window position. This setting is optional, and will only be used
     * if PJMEDIA_VID_DEV_CAP_OUTPUT_POSITION is set in the flags.
     */
    pjmedia_coord window_pos;

    /**
     * Video window's visibility. This setting is optional, and will only be
     * used if PJMEDIA_VID_DEV_CAP_OUTPUT_HIDE is set in the flags.
     */
    pj_bool_t window_hide;

    /**
     * Enable built-in preview. This setting is optional and is only used
     * if PJMEDIA_VID_DEV_CAP_INPUT_PREVIEW capability is supported and
     * set in the flags.
     */
    pj_bool_t native_preview;

    /**
     * Video orientation. This setting is optional and is only used if
     * PJMEDIA_VID_DEV_CAP_ORIENTATION capability is supported and is
     * set in the flags.
     */
    pjmedia_orient orient;

    /**
     * Video window flags. This setting is optional, and will only be used
     * if PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW_FLAGS is set in the flags.
     */
    unsigned window_flags;

} pjmedia_vid_dev_param;


/** Forward declaration for video device factory */
typedef struct pjmedia_vid_dev_factory pjmedia_vid_dev_factory;

/* typedef for factory creation function */
typedef pjmedia_vid_dev_factory*
(*pjmedia_vid_dev_factory_create_func_ptr)(pj_pool_factory*);

/**
 * Initialize pjmedia_vid_dev_switch_param.
 *
 * @param p	    Parameter to be initialized.
 */
PJ_INLINE(void)
pjmedia_vid_dev_switch_param_default(pjmedia_vid_dev_switch_param *p)
{
    pj_bzero(p, sizeof(*p));
    p->target_id = PJMEDIA_VID_INVALID_DEV;
}

/**
 * Get string info for the specified capability.
 *
 * @param cap       The capability ID.
 * @param p_desc    Optional pointer which will be filled with longer
 *                  description about the capability.
 *
 * @return          Capability name.
 */
PJ_DECL(const char*) pjmedia_vid_dev_cap_name(pjmedia_vid_dev_cap cap,
                                              const char **p_desc);


/**
 * Set a capability field value in #pjmedia_vid_dev_param structure. This will
 * also set the flags field for the specified capability in the structure.
 *
 * @param param     The structure.
 * @param cap       The video capability which value is to be set.
 * @param pval      Pointer to value. Please see the type of value to
 *                  be supplied in the pjmedia_vid_dev_cap documentation.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t)
pjmedia_vid_dev_param_set_cap(pjmedia_vid_dev_param *param,
                              pjmedia_vid_dev_cap cap,
                              const void *pval);


/**
 * Get a capability field value from #pjmedia_vid_dev_param structure. This
 * function will return PJMEDIA_EVID_INVCAP error if the flag for that
 * capability is not set in the flags field in the structure.
 *
 * @param param     The structure.
 * @param cap       The video capability which value is to be retrieved.
 * @param pval      Pointer to value. Please see the type of value to
 *                  be supplied in the pjmedia_vid_dev_cap documentation.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t)
pjmedia_vid_dev_param_get_cap(const pjmedia_vid_dev_param *param,
                              pjmedia_vid_dev_cap cap,
                              void *pval);

/**
 * Initialize the video device subsystem. This will register all supported
 * video device factories to the video device subsystem. This function may be
 * called more than once, but each call to this function must have the
 * corresponding #pjmedia_vid_dev_subsys_shutdown() call.
 *
 * @param pf        The pool factory.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_subsys_init(pj_pool_factory *pf);


/**
 * Get the pool factory registered to the video device subsystem.
 *
 * @return          The pool factory.
 */
PJ_DECL(pj_pool_factory*) pjmedia_vid_dev_subsys_get_pool_factory(void);


/**
 * Shutdown the video device subsystem. This will destroy all video device
 * factories registered in the video device subsystem. Note that currently
 * opened video streams may or may not be closed, depending on the
 * implementation of the video device factories.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_subsys_shutdown(void);


/**
 * Register a supported video device factory to the video device subsystem.
 * Application can either register a function to create the factory, or
 * an instance of an already created factory.
 *
 * This function can only be called after calling
 * #pjmedia_vid_dev_subsys_init().
 *
 * @param vdf       The factory creation function. Either vdf or factory
 * 		    argument must be specified.
 * @param factory   Factory instance. Either vdf or factory
 * 		    argument must be specified.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t)
pjmedia_vid_register_factory(pjmedia_vid_dev_factory_create_func_ptr vdf,
                             pjmedia_vid_dev_factory *factory);


/**
 * Unregister a video device factory from the video device subsystem. This
 * function can only be called after calling #pjmedia_vid_dev_subsys_init().
 * Devices from this factory will be unlisted. If a device from this factory
 * is currently in use, then the behavior is undefined.
 *
 * @param vdf       The video device factory. Either vdf or factory argument
 * 		    must be specified.
 * @param factory   The factory instance. Either vdf or factory argument
 * 		    must be specified.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t)
pjmedia_vid_unregister_factory(pjmedia_vid_dev_factory_create_func_ptr vdf,
                               pjmedia_vid_dev_factory *factory);


/**
 * Refresh the list of video devices installed in the system. This function
 * will only refresh the list of videoo device so all active video streams will
 * be unaffected. After refreshing the device list, application MUST make sure
 * to update all index references to video devices (i.e. all variables of type
 * pjmedia_vid_dev_index) before calling any function that accepts video device
 * index as its parameter.
 *
 * @return		PJ_SUCCESS on successful operation or the appropriate
 *			error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_refresh(void);


/**
 * Get the number of video devices installed in the system.
 *
 * @return          The number of video devices installed in the system.
 */
PJ_DECL(unsigned) pjmedia_vid_dev_count(void);


/**
 * Get device information.
 *
 * @param id        The video device ID.
 * @param info      The device information which will be filled in by this
 *                  function once it returns successfully.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_get_info(pjmedia_vid_dev_index id,
                                              pjmedia_vid_dev_info *info);


/**
 * Lookup device index based on the driver and device name.
 *
 * @param drv_name  The driver name.
 * @param dev_name  The device name.
 * @param id        Pointer to store the returned device ID.
 *
 * @return          PJ_SUCCESS if the device can be found.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_lookup(const char *drv_name,
                                            const char *dev_name,
                                            pjmedia_vid_dev_index *id);


/**
 * Initialize the video device parameters with default values for the
 * specified device.
 *
 * @param id        The video device ID.
 * @param param     The video device parameters which will be initialized
 *                  by this function once it returns successfully.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t)
pjmedia_vid_dev_default_param(pj_pool_t *pool,
                              pjmedia_vid_dev_index id,
                              pjmedia_vid_dev_param *param);


/**
 * Open video stream object using the specified parameters. If stream is
 * created successfully, this function will return PJ_SUCCESS and the
 * stream pointer will be returned in the p_strm argument.
 *
 * The opened stream may have been opened with different size and fps
 * than the requested values in the \a param argument. Application should
 * check the actual size and fps that the stream was opened with by inspecting
 * the values in the \a param argument and see if they have changed. Also
 * if the device ID in the \a param specifies default device, it may be
 * replaced with the actual device ID upon return.
 *
 * @param param         Sound device parameters to be used for the stream.
 * @param cb            Pointer to structure containing video stream
 *                      callbacks.
 * @param user_data     Arbitrary user data, which will be given back in the
 *                      callbacks.
 * @param p_strm        Pointer to receive the video stream.
 *
 * @return              PJ_SUCCESS on successful operation or the appropriate
 *                      error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_stream_create(
					    pjmedia_vid_dev_param *param,
					    const pjmedia_vid_dev_cb *cb,
					    void *user_data,
					    pjmedia_vid_dev_stream **p_strm);

/**
 * Get the running parameters for the specified video stream.
 *
 * @param strm      The video stream.
 * @param param     Video stream parameters to be filled in by this
 *                  function once it returns successfully.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_stream_get_param(
					    pjmedia_vid_dev_stream *strm,
                                            pjmedia_vid_dev_param *param);

/**
 * Get the value of a specific capability of the video stream.
 *
 * @param strm      The video stream.
 * @param cap       The video capability which value is to be retrieved.
 * @param value     Pointer to value to be filled in by this function
 *                  once it returns successfully.  Please see the type
 *                  of value to be supplied in the pjmedia_vid_dev_cap
 *                  documentation.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_stream_get_cap(
					    pjmedia_vid_dev_stream *strm,
					    pjmedia_vid_dev_cap cap,
                                            void *value);

/**
 * Set the value of a specific capability of the video stream.
 *
 * @param strm      The video stream.
 * @param cap       The video capability which value is to be set.
 * @param value     Pointer to value. Please see the type of value to
 *                  be supplied in the pjmedia_vid_dev_cap documentation.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_stream_set_cap(
					    pjmedia_vid_dev_stream *strm,
					    pjmedia_vid_dev_cap cap,
					    const void *value);

/**
 * Start the stream.
 *
 * @param strm      The video stream.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_stream_start(
					    pjmedia_vid_dev_stream *strm);

/**
 * Query whether the stream has been started.
 *
 * @param strm	    The video stream
 *
 * @return	    PJ_TRUE if the video stream has been started.
 */
PJ_DECL(pj_bool_t) pjmedia_vid_dev_stream_is_running(pjmedia_vid_dev_stream *strm);


/**
 * Request one frame from the stream. Application needs to call this function
 * periodically only if the stream doesn't support "active interface", i.e.
 * the pjmedia_vid_dev_info.has_callback member is PJ_FALSE.
 *
 * @param strm      The video stream.
 * @param frame	    The video frame to be filled by the device.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_stream_get_frame(
					    pjmedia_vid_dev_stream *strm,
                                            pjmedia_frame *frame);

/**
 * Put one frame to the stream. Application needs to call this function
 * periodically only if the stream doesn't support "active interface", i.e.
 * the pjmedia_vid_dev_info.has_callback member is PJ_FALSE.
 *
 * @param strm      The video stream.
 * @param frame	    The video frame to put to the device.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_stream_put_frame(
					    pjmedia_vid_dev_stream *strm,
                                            const pjmedia_frame *frame);

/**
 * Stop the stream.
 *
 * @param strm      The video stream.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_stream_stop(
					    pjmedia_vid_dev_stream *strm);

/**
 * Destroy the stream.
 *
 * @param strm      The video stream.
 *
 * @return          PJ_SUCCESS on successful operation or the appropriate
 *                  error code.
 */
PJ_DECL(pj_status_t) pjmedia_vid_dev_stream_destroy(
					    pjmedia_vid_dev_stream *strm);


/**
 * @}
 */

PJ_END_DECL


#endif    /* __PJMEDIA_VIDEODEV_VIDEODEV_H__ */
