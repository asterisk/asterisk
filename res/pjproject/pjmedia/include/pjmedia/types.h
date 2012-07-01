/* $Id$ */
/* 
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
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
#ifndef __PJMEDIA_TYPES_H__
#define __PJMEDIA_TYPES_H__

/**
 * @file pjmedia/types.h Basic Types
 * @brief Basic PJMEDIA types.
 */

#include <pjmedia/config.h>
#include <pj/sock.h>
#include <pj/types.h>


/**
 * @defgroup PJMEDIA_PORT Media Ports Framework
 * @brief Extensible framework for media terminations
 */


/**
 * @defgroup PJMEDIA_FRAME_OP Audio Manipulation Algorithms
 * @brief Algorithms to manipulate audio frames
 */

/**
 * @defgroup PJMEDIA_TYPES Basic Types
 * @ingroup PJMEDIA_BASE
 * @brief Basic PJMEDIA types and operations.
 * @{
 */

/**
 * Top most media type. See also #pjmedia_type_name().
 */
typedef enum pjmedia_type
{
    /** Type is not specified. */
    PJMEDIA_TYPE_NONE,

    /** The media is audio */
    PJMEDIA_TYPE_AUDIO,

    /** The media is video. */
    PJMEDIA_TYPE_VIDEO,

    /** The media is application. */
    PJMEDIA_TYPE_APPLICATION,

    /** The media type is unknown or unsupported. */
    PJMEDIA_TYPE_UNKNOWN

} pjmedia_type;


/**
 * Media transport protocol.
 */
typedef enum pjmedia_tp_proto
{
    /** No transport type */
    PJMEDIA_TP_PROTO_NONE = 0,

    /** RTP using A/V profile */
    PJMEDIA_TP_PROTO_RTP_AVP,

    /** Secure RTP */
    PJMEDIA_TP_PROTO_RTP_SAVP,

    /** Unknown */
    PJMEDIA_TP_PROTO_UNKNOWN

} pjmedia_tp_proto;


/**
 * Media direction.
 */
typedef enum pjmedia_dir
{
    /** None */
    PJMEDIA_DIR_NONE = 0,

    /** Encoding (outgoing to network) stream, also known as capture */
    PJMEDIA_DIR_ENCODING = 1,

    /** Same as encoding direction. */
    PJMEDIA_DIR_CAPTURE = PJMEDIA_DIR_ENCODING,

    /** Decoding (incoming from network) stream, also known as playback. */
    PJMEDIA_DIR_DECODING = 2,

    /** Same as decoding. */
    PJMEDIA_DIR_PLAYBACK = PJMEDIA_DIR_DECODING,

    /** Same as decoding. */
    PJMEDIA_DIR_RENDER = PJMEDIA_DIR_DECODING,

    /** Incoming and outgoing stream, same as PJMEDIA_DIR_CAPTURE_PLAYBACK */
    PJMEDIA_DIR_ENCODING_DECODING = 3,

    /** Same as ENCODING_DECODING */
    PJMEDIA_DIR_CAPTURE_PLAYBACK = PJMEDIA_DIR_ENCODING_DECODING,

    /** Same as ENCODING_DECODING */
    PJMEDIA_DIR_CAPTURE_RENDER = PJMEDIA_DIR_ENCODING_DECODING

} pjmedia_dir;


/**
 * Opaque declaration of media endpoint.
 */
typedef struct pjmedia_endpt pjmedia_endpt;

/*
 * Forward declaration for stream (needed by transport).
 */
typedef struct pjmedia_stream pjmedia_stream;

/**
 * Enumeration for picture coordinate base.
 */
typedef enum pjmedia_coord_base
{
    /**
     * This specifies that the pixel [0, 0] location is at the left-top
     * position.
     */
    PJMEDIA_COORD_BASE_LEFT_TOP,

    /**
     * This specifies that the pixel [0, 0] location is at the left-bottom
     * position.
     */
    PJMEDIA_COORD_BASE_LEFT_BOTTOM

} pjmedia_coord_base;

/**
 * This structure is used to represent rational numbers.
 */
typedef struct pjmedia_ratio
{
    int		num;    /** < Numerator. */
    int		denum;  /** < Denumerator. */
} pjmedia_ratio;

/**
 * This structure represent a coordinate.
 */
typedef struct pjmedia_coord
{
    int		x;	/**< X position of the coordinate */
    int		y;	/**< Y position of the coordinate */
} pjmedia_coord;

/**
 * This structure represents rectangle size.
 */
typedef struct pjmedia_rect_size
{
    unsigned	w;	/**< The width.		*/
    unsigned 	h;	/**< The height.	*/
} pjmedia_rect_size;

/**
 * This structure describes a rectangle.
 */
typedef struct pjmedia_rect
{
    pjmedia_coord	coord;	/**< The position.	*/
    pjmedia_rect_size	size;	/**< The size.		*/
} pjmedia_rect;

/**
 * Enumeration for video/picture orientation.
 */
typedef enum pjmedia_orient
{
    /**
     * Unknown orientation.
     */
    PJMEDIA_ORIENT_UNKNOWN,

    /**
     * Natural orientation, e.g: sky upside on landscape view, head upside
     * on human portrait.
     */
    PJMEDIA_ORIENT_NATURAL,

    /**
     * Specifies that the video/picture needs to be rotated 90 degrees
     * clockwise to be displayed in natural orientation.
     */
    PJMEDIA_ORIENT_ROTATE_90DEG,

    /**
     * Specifies that the video/picture needs to be rotated 180 degrees
     * clockwise to be displayed in natural orientation.
     */
    PJMEDIA_ORIENT_ROTATE_180DEG,

    /**
     * Specifies that the video/picture needs to be rotated 270 degrees
     * clockwise to be displayed in natural orientation.
     */
    PJMEDIA_ORIENT_ROTATE_270DEG

} pjmedia_orient;


/**
 * Macro for packing format from a four character code, similar to FOURCC.
 */
#define PJMEDIA_FOURCC(C1, C2, C3, C4) ( C4<<24 | C3<<16 | C2<<8 | C1 )


/**
 * Utility function to return the string name for a pjmedia_type.
 *
 * @param t		The media type.
 *
 * @return		String.
 */
PJ_DECL(const char*) pjmedia_type_name(pjmedia_type t);

/**
 * A utility function to convert fourcc type of value to four letters string.
 *
 * @param sig		The fourcc value.
 * @param buf		Buffer to store the string, which MUST be at least
 * 			five bytes long.
 *
 * @return		The string.
 */
PJ_INLINE(const char*) pjmedia_fourcc_name(pj_uint32_t sig, char buf[])
{
    buf[3] = (char)((sig >> 24) & 0xFF);
    buf[2] = (char)((sig >> 16) & 0xFF);
    buf[1] = (char)((sig >>  8) & 0xFF);
    buf[0] = (char)((sig >>  0) & 0xFF);
    buf[4] = '\0';
    return buf;
}


/**
 * @}
 */


#endif	/* __PJMEDIA_TYPES_H__ */

