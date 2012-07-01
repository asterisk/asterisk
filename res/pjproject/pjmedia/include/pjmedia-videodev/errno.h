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
#ifndef __PJMEDIA_VIDEODEV_VIDEODEV_ERRNO_H__
#define __PJMEDIA_VIDEODEV_VIDEODEV_ERRNO_H__

/**
 * @file errno.h Error Codes
 * @brief Videodev specific error codes.
 */

#include <pjmedia-videodev/config.h>
#include <pj/errno.h>

/**
 * @defgroup error_codes Error Codes
 * @ingroup video_device_api
 * @brief Video device library specific error codes.
 * @{
 */


PJ_BEGIN_DECL


/**
 * Start of error code relative to PJ_ERRNO_START_USER.
 * This value is 520000.
 */
#define PJMEDIA_VIDEODEV_ERRNO_START \
	    (PJ_ERRNO_START_USER + PJ_ERRNO_SPACE_SIZE*7)
#define PJMEDIA_VIDEODEV_ERRNO_END   \
	    (PJMEDIA_VIDEODEV_ERRNO_START + PJ_ERRNO_SPACE_SIZE - 1)


/************************************************************
 * Video Device API error codes
 ***********************************************************/
/**
 * @hideinitializer
 * General/unknown error.
 */
#define PJMEDIA_EVID_ERR	(PJMEDIA_VIDEODEV_ERRNO_START+1) /* 520001 */

/**
 * @hideinitializer
 * Unknown error from video driver
 */
#define PJMEDIA_EVID_SYSERR	(PJMEDIA_VIDEODEV_ERRNO_START+2) /* 520002 */

/**
 * @hideinitializer
 * Video subsystem not initialized
 */
#define PJMEDIA_EVID_INIT	(PJMEDIA_VIDEODEV_ERRNO_START+3) /* 520003 */

/**
 * @hideinitializer
 * Invalid video device
 */
#define PJMEDIA_EVID_INVDEV	(PJMEDIA_VIDEODEV_ERRNO_START+4) /* 520004 */

/**
 * @hideinitializer
 * Found no devices
 */
#define PJMEDIA_EVID_NODEV	(PJMEDIA_VIDEODEV_ERRNO_START+5) /* 520005 */

/**
 * @hideinitializer
 * Unable to find default device
 */
#define PJMEDIA_EVID_NODEFDEV	(PJMEDIA_VIDEODEV_ERRNO_START+6) /* 520006 */

/**
 * @hideinitializer
 * Device not ready
 */
#define PJMEDIA_EVID_NOTREADY	(PJMEDIA_VIDEODEV_ERRNO_START+7) /* 520007 */

/**
 * @hideinitializer
 * The video capability is invalid or not supported
 */
#define PJMEDIA_EVID_INVCAP	(PJMEDIA_VIDEODEV_ERRNO_START+8) /* 520008 */

/**
 * @hideinitializer
 * The operation is invalid or not supported
 */
#define PJMEDIA_EVID_INVOP	(PJMEDIA_VIDEODEV_ERRNO_START+9) /* 520009 */

/**
 * @hideinitializer
 * Bad or invalid video device format
 */
#define PJMEDIA_EVID_BADFORMAT	(PJMEDIA_VIDEODEV_ERRNO_START+10) /* 520010 */

/**
 * @hideinitializer
 * Invalid video device sample format
 */
#define PJMEDIA_EVID_SAMPFORMAT	(PJMEDIA_VIDEODEV_ERRNO_START+11) /* 520011 */

/**
 * @hideinitializer
 * Bad latency setting
 */
#define PJMEDIA_EVID_BADLATENCY	(PJMEDIA_VIDEODEV_ERRNO_START+12) /* 520012 */

/**
 * @hideinitializer
 * Bad/unsupported video size
 */
#define PJMEDIA_EVID_BADSIZE	(PJMEDIA_VIDEODEV_ERRNO_START+13) /* 520013 */


/**
 * Get error message for the specified error code. Note that this
 * function is only able to decode PJMEDIA Videodev specific error code.
 * Application should use pj_strerror(), which should be able to
 * decode all error codes belonging to all subsystems (e.g. pjlib,
 * pjmedia, pjsip, etc).
 *
 * @param status    The error code.
 * @param buffer    The buffer where to put the error message.
 * @param bufsize   Size of the buffer.
 *
 * @return	    The error message as NULL terminated string,
 *                  wrapped with pj_str_t.
 */
PJ_DECL(pj_str_t) pjmedia_videodev_strerror(pj_status_t status, char *buffer,
					    pj_size_t bufsize);


PJ_END_DECL

/**
 * @}
 */


#endif	/* __PJMEDIA_VIDEODEV_VIDEODEV_ERRNO_H__ */

