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
#ifndef __PJMEDIA_VIDEODEV_CONFIG_H__
#define __PJMEDIA_VIDEODEV_CONFIG_H__

/**
 * @file config.h
 * @brief Video config.
 */
#include <pjmedia/types.h>
#include <pj/pool.h>


PJ_BEGIN_DECL

/**
 * @defgroup video_device_api Video Device API
 * @brief PJMEDIA video device abstraction API.
 */

/**
 * @defgroup s1_video_device_config Compile time configurations
 * @ingroup video_device_api
 * @brief Compile time configurations
 * @{
 */

/**
 * This setting controls the maximum number of formats that can be
 * supported by a video device.
 *
 * Default: 16
 */
#ifndef PJMEDIA_VID_DEV_INFO_FMT_CNT
#   define PJMEDIA_VID_DEV_INFO_FMT_CNT 16
#endif


/**
 * This setting controls whether SDL support should be included.
 *
 * Default: 0 (or detected by configure)
 */
#ifndef PJMEDIA_VIDEO_DEV_HAS_SDL
#   define PJMEDIA_VIDEO_DEV_HAS_SDL		0
#   define PJMEDIA_VIDEO_DEV_SDL_HAS_OPENGL	0
#endif


/**
 * This setting controls whether QT support should be included.
 *
 * Default: 0 (or detected by configure)
 */
#ifndef PJMEDIA_VIDEO_DEV_HAS_QT
#   define PJMEDIA_VIDEO_DEV_HAS_QT		0
#endif


/**
 * This setting controls whether IOS support should be included.
 *
 * Default: 0 (or detected by configure)
 */
#ifndef PJMEDIA_VIDEO_DEV_HAS_IOS
#   define PJMEDIA_VIDEO_DEV_HAS_IOS		0
#endif


/**
 * This setting controls whether Direct Show support should be included.
 *
 * Default: 0 (unfinished)
 */
#ifndef PJMEDIA_VIDEO_DEV_HAS_DSHOW
#   define PJMEDIA_VIDEO_DEV_HAS_DSHOW		0 //PJ_WIN32
#endif


/**
 * This setting controls whether colorbar source support should be included.
 *
 * Default: 1
 */
#ifndef PJMEDIA_VIDEO_DEV_HAS_CBAR_SRC
#   define PJMEDIA_VIDEO_DEV_HAS_CBAR_SRC	1
#endif


/**
 * This setting controls whether ffmpeg support should be included.
 *
 * Default: 0 (unfinished)
 */
#ifndef PJMEDIA_VIDEO_DEV_HAS_FFMPEG
#   define PJMEDIA_VIDEO_DEV_HAS_FFMPEG	        0
#endif


/**
 * Video4Linux2
 *
 * Default: 0 (or detected by configure)
 */
#ifndef PJMEDIA_VIDEO_DEV_HAS_V4L2
#   define PJMEDIA_VIDEO_DEV_HAS_V4L2		0
#endif


/**
 * Enable support for AVI player virtual capture device.
 *
 * Default: 1
 */
#ifndef PJMEDIA_VIDEO_DEV_HAS_AVI
#   define PJMEDIA_VIDEO_DEV_HAS_AVI		1
#endif


/**
 * @}
 */

PJ_END_DECL


#endif	/* __PJMEDIA_VIDEODEV_CONFIG_H__ */

/*
 --------------------- DOCUMENTATION FOLLOWS ---------------------------
 */

/**
 * @addtogroup video_device_api Video Device API
 * @{

PJMEDIA Video Device API is a cross-platform video API appropriate for use with
VoIP applications and many other types of video streaming applications. 

The API abstracts many different video API's on various platforms, such as:
 - native Direct Show video for Win32 and Windows Mobile devices
 - null-video implementation
 - and more to be implemented in the future

The Video Device API/library is an evolution from PJMEDIA @ref PJMED_SND and 
contains many enhancements:

 - Forward compatibility:
\n
   The new API has been designed to be extensible, it will support new API's as 
   well as new features that may be introduced in the future without breaking 
   compatibility with applications that use this API as well as compatibility 
   with existing device implementations. 

 - Device capabilities:
\n
   At the heart of the API is device capabilities management, where all possible
   video capabilities of video devices should be able to be handled in a generic
   manner. With this framework, new capabilities that may be discovered in the 
   future can be handled in manner without breaking existing applications. 

 - Built-in features:
\n
   The device capabilities framework enables applications to use and control 
   video features built-in in the device, such as:
    - built-in formats, 
    - etc.

 - Codec support:
\n
   Some video devices support built-in hardware video codecs, and application
   can use the video device in encoded mode to make use of these hardware 
   codecs. 

 - Multiple backends:
\n
   The new API supports multiple video backends (called factories or drivers in 
   the code) to be active simultaneously, and video backends may be added or 
   removed during run-time. 

*/


/**
 * @}
 */

