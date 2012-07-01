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
#ifndef PJMEDIA_VIDEODEV_AVI_DEV_H__
#define PJMEDIA_VIDEODEV_AVI_DEV_H__

/**
 * @file avi_dev.h
 * @brief AVI player virtual device
 */
#include <pjmedia-videodev/videodev.h>
#include <pjmedia/avi_stream.h>

PJ_BEGIN_DECL

/**
 * @defgroup avi_dev AVI Player Virtual Device
 * @ingroup video_device_api
 * @brief AVI player virtual device
 * @{
 * This describes a virtual capture device which takes its input from an AVI
 * file.
 */

/**
 * Settings for the AVI player virtual device. This param corresponds to
 * PJMEDIA_VID_DEV_CAP_AVI_PLAY capability of the video device/stream.
 */
typedef struct pjmedia_avi_dev_param
{
    /**
     * Specifies the full path of the AVI file to be played.
     */
    pj_str_t	path;

    /**
     * If this setting is specified when setting the device, this specifies
     * the title to be assigned as the device name. If this setting not
     * specified, the filename part of the path will be used.
     */
    pj_str_t	title;

    /**
     * The underlying AVI streams created by the device. If the value is NULL,
     * that means the device has not been configured yet. Application can use
     * this field to retrieve the audio stream of the AVI. This setting is
     * "get"-only and will be ignored in "set capability" operation.
     */
    pjmedia_avi_streams *avi_streams;

} pjmedia_avi_dev_param;


/**
 * Reset pjmedia_avi_dev_param with the default settings. This mostly will
 * reset all values to NULL or zero.
 *
 * @param p	The parameter to be initialized.
 */
PJ_DECL(void) pjmedia_avi_dev_param_default(pjmedia_avi_dev_param *p);


/**
 * Create a AVI device factory, and register it to the video device
 * subsystem. At least one factory needs to be created before an AVI
 * device can be allocated and used, and normally only one factory is
 * needed per application.
 *
 * @param pf		Pool factory to be used.
 * @param max_dev	Number of devices to be reserved.
 * @param p_ret		Pointer to return the factory instance, to be
 * 			used when allocating a virtual device.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_avi_dev_create_factory(
				    pj_pool_factory *pf,
				    unsigned max_dev,
				    pjmedia_vid_dev_factory **p_ret);

/**
 * Allocate one device ID to be used to play the specified AVI file in
 * the parameter.
 *
 * @param param		The parameter, with at least the AVI file path
 * 			set.
 * @param p_id		Optional pointer to receive device ID to play
 * 			the file.
 *
 * @return		PJ_SUCCESS or the appropriate error code.
 *
 */
PJ_DECL(pj_status_t) pjmedia_avi_dev_alloc(pjmedia_vid_dev_factory *f,
                                           pjmedia_avi_dev_param *param,
                                           pjmedia_vid_dev_index *p_id);

/**
 * Retrieve the parameters set for the virtual device.
 *
 * @param id		Device ID.
 * @param prm		Structure to receive the settings.
 *
 * @return		PJ_SUCCESS or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_avi_dev_get_param(pjmedia_vid_dev_index id,
                                               pjmedia_avi_dev_param *param);

/**
 * Free the resources associated with the virtual device.
 *
 * @param id		The device ID.
 *
 * @return		PJ_SUCCESS or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_avi_dev_free(pjmedia_vid_dev_index id);

/**
 * @}
 */

PJ_END_DECL


#endif    /* PJMEDIA_VIDEODEV_AVI_DEV_H__ */
