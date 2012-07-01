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
#ifndef __PJMEDIA_FORMAT_H__
#define __PJMEDIA_FORMAT_H__

/**
 * @file pjmedia/format.h Media format
 * @brief Media format
 */
#include <pjmedia/types.h>

/**
 * @defgroup PJMEDIA_FORMAT Media format
 * @ingroup PJMEDIA_TYPES
 * @brief Media format
 * @{
 */

PJ_BEGIN_DECL

/**
 * Macro for packing format from a four character code, similar to FOURCC.
 * This macro is used for building the constants in pjmedia_format_id
 * enumeration.
 */
#define PJMEDIA_FORMAT_PACK(C1, C2, C3, C4) PJMEDIA_FOURCC(C1, C2, C3, C4)

/**
 * This enumeration uniquely identify audio sample and/or video pixel formats.
 * Some well known formats are listed here. The format ids are built by
 * combining four character codes, similar to FOURCC. The format id is
 * extensible, as application may define and use format ids not declared
 * on this enumeration.
 *
 * This format id along with other information will fully describe the media
 * in #pjmedia_format structure.
 */
typedef enum pjmedia_format_id
{
    /*
     * Audio formats
     */

    /** 16bit signed integer linear PCM audio */
    PJMEDIA_FORMAT_L16	    = 0,

    /** Alias for PJMEDIA_FORMAT_L16 */
    PJMEDIA_FORMAT_PCM	    = PJMEDIA_FORMAT_L16,

    /** G.711 ALAW */
    PJMEDIA_FORMAT_PCMA	    = PJMEDIA_FORMAT_PACK('A', 'L', 'A', 'W'),

    /** Alias for PJMEDIA_FORMAT_PCMA */
    PJMEDIA_FORMAT_ALAW	    = PJMEDIA_FORMAT_PCMA,

    /** G.711 ULAW */
    PJMEDIA_FORMAT_PCMU	    = PJMEDIA_FORMAT_PACK('u', 'L', 'A', 'W'),

    /** Aliaw for PJMEDIA_FORMAT_PCMU */
    PJMEDIA_FORMAT_ULAW	    = PJMEDIA_FORMAT_PCMU,

    /** AMR narrowband */
    PJMEDIA_FORMAT_AMR	    = PJMEDIA_FORMAT_PACK(' ', 'A', 'M', 'R'),

    /** ITU G.729 */
    PJMEDIA_FORMAT_G729	    = PJMEDIA_FORMAT_PACK('G', '7', '2', '9'),

    /** Internet Low Bit-Rate Codec (ILBC) */
    PJMEDIA_FORMAT_ILBC	    = PJMEDIA_FORMAT_PACK('I', 'L', 'B', 'C'),


    /*
     * Video formats.
     */
    /**
     * 24bit RGB
     */
    PJMEDIA_FORMAT_RGB24    = PJMEDIA_FORMAT_PACK('R', 'G', 'B', '3'),

    /**
     * 32bit RGB with alpha channel
     */
    PJMEDIA_FORMAT_RGBA     = PJMEDIA_FORMAT_PACK('R', 'G', 'B', 'A'),
    PJMEDIA_FORMAT_BGRA     = PJMEDIA_FORMAT_PACK('B', 'G', 'R', 'A'),

    /**
     * Alias for PJMEDIA_FORMAT_RGBA
     */
    PJMEDIA_FORMAT_RGB32    = PJMEDIA_FORMAT_RGBA,

    /**
     * Device Independent Bitmap, alias for 24 bit RGB
     */
    PJMEDIA_FORMAT_DIB      = PJMEDIA_FORMAT_PACK('D', 'I', 'B', ' '),

    /**
     * This is planar 4:4:4/24bpp RGB format, the data can be treated as
     * three planes of color components, where the first plane contains
     * only the G samples, the second plane contains only the B samples,
     * and the third plane contains only the R samples.
     */
    PJMEDIA_FORMAT_GBRP    = PJMEDIA_FORMAT_PACK('G', 'B', 'R', 'P'),

    /**
     * This is a packed 4:4:4/32bpp format, where each pixel is encoded as
     * four consecutive bytes, arranged in the following sequence: V0, U0,
     * Y0, A0. Source:
     * http://msdn.microsoft.com/en-us/library/dd206750%28v=VS.85%29.aspx#ayuv
     */
    PJMEDIA_FORMAT_AYUV	    = PJMEDIA_FORMAT_PACK('A', 'Y', 'U', 'V'),

    /**
     * This is packed 4:2:2/16bpp YUV format, the data can be treated as
     * an array of unsigned char values, where the first byte contains
     * the first Y sample, the second byte contains the first U (Cb) sample,
     * the third byte contains the second Y sample, and the fourth byte
     * contains the first V (Cr) sample, and so forth. Source:
     * http://msdn.microsoft.com/en-us/library/dd206750%28v=VS.85%29.aspx#yuy2
     */
    PJMEDIA_FORMAT_YUY2	    = PJMEDIA_FORMAT_PACK('Y', 'U', 'Y', '2'),

    /**
     * This format is the same as the YUY2 format except the byte order is
     * reversed -- that is, the chroma and luma bytes are flipped. If the
     * image is addressed as an array of two little-endian WORD values, the
     * first WORD contains U in the LSBs and Y0 in the MSBs, and the second
     * WORD contains V in the LSBs and Y1 in the MSBs. Source:
     * http://msdn.microsoft.com/en-us/library/dd206750%28v=VS.85%29.aspx#uyvy
     */
    PJMEDIA_FORMAT_UYVY	    = PJMEDIA_FORMAT_PACK('U', 'Y', 'V', 'Y'),

    /**
     * This format is the same as the YUY2 and UYVY format except the byte
     * order is reversed -- that is, the chroma and luma bytes are flipped.
     * If the image is addressed as an array of two little-endian WORD values,
     * the first WORD contains Y0 in the LSBs and V in the MSBs, and the second
     * WORD contains Y1 in the LSBs and U in the MSBs.
     */
    PJMEDIA_FORMAT_YVYU	    = PJMEDIA_FORMAT_PACK('Y', 'V', 'Y', 'U'),

    /**
     * This is planar 4:2:0/12bpp YUV format, the data can be treated as
     * three planes of color components, where the first plane contains
     * only the Y samples, the second plane contains only the U (Cb) samples,
     * and the third plane contains only the V (Cr) sample.
     */
    PJMEDIA_FORMAT_I420	    = PJMEDIA_FORMAT_PACK('I', '4', '2', '0'),

    /**
     * IYUV is alias for I420.
     */
    PJMEDIA_FORMAT_IYUV	    = PJMEDIA_FORMAT_I420,

    /**
     * This is planar 4:2:2/16bpp YUV format.
     */
    PJMEDIA_FORMAT_YV12	    = PJMEDIA_FORMAT_PACK('Y', 'V', '1', '2'),

    /**
     * The JPEG version of planar 4:2:0/12bpp YUV format.
     */
    PJMEDIA_FORMAT_I420JPEG = PJMEDIA_FORMAT_PACK('J', '4', '2', '0'),

    /**
     * The JPEG version of planar 4:2:2/16bpp YUV format.
     */
    PJMEDIA_FORMAT_I422JPEG = PJMEDIA_FORMAT_PACK('J', '4', '2', '2'),

    /**
     * Encoded video formats
     */

    PJMEDIA_FORMAT_H261     = PJMEDIA_FORMAT_PACK('H', '2', '6', '1'),
    PJMEDIA_FORMAT_H263     = PJMEDIA_FORMAT_PACK('H', '2', '6', '3'),
    PJMEDIA_FORMAT_H263P    = PJMEDIA_FORMAT_PACK('P', '2', '6', '3'),
    PJMEDIA_FORMAT_H264     = PJMEDIA_FORMAT_PACK('H', '2', '6', '4'),

    PJMEDIA_FORMAT_MJPEG    = PJMEDIA_FORMAT_PACK('M', 'J', 'P', 'G'),
    PJMEDIA_FORMAT_MPEG1VIDEO = PJMEDIA_FORMAT_PACK('M', 'P', '1', 'V'),
    PJMEDIA_FORMAT_MPEG2VIDEO = PJMEDIA_FORMAT_PACK('M', 'P', '2', 'V'),
    PJMEDIA_FORMAT_MPEG4    = PJMEDIA_FORMAT_PACK('M', 'P', 'G', '4'),

} pjmedia_format_id;

/**
 * This enumeration specifies what type of detail is included in a
 * #pjmedia_format structure.
 */
typedef enum pjmedia_format_detail_type
{
    /** Format detail is not specified. */
    PJMEDIA_FORMAT_DETAIL_NONE,

    /** Audio format detail. */
    PJMEDIA_FORMAT_DETAIL_AUDIO,

    /** Video format detail. */
    PJMEDIA_FORMAT_DETAIL_VIDEO,

    /** Number of format detail type that has been defined. */
    PJMEDIA_FORMAT_DETAIL_MAX

} pjmedia_format_detail_type;

/**
 * This structure is put in \a detail field of #pjmedia_format to describe
 * detail information about an audio media.
 */
typedef struct pjmedia_audio_format_detail
{
    unsigned	clock_rate;	/**< Audio clock rate in samples or Hz. */
    unsigned	channel_count;	/**< Number of channels.		*/
    unsigned	frame_time_usec;/**< Frame interval, in microseconds.	*/
    unsigned	bits_per_sample;/**< Number of bits per sample.		*/
    pj_uint32_t	avg_bps;	/**< Average bitrate			*/
    pj_uint32_t	max_bps;	/**< Maximum bitrate			*/
} pjmedia_audio_format_detail;

/**
 * This structure is put in \a detail field of #pjmedia_format to describe
 * detail information about a video media.
 *
 * Additional information about a video format can also be retrieved by
 * calling #pjmedia_get_video_format_info().
 */
typedef struct pjmedia_video_format_detail
{
    pjmedia_rect_size	size;	/**< Video size (width, height) 	*/
    pjmedia_ratio	fps;	/**< Number of frames per second.	*/
    pj_uint32_t		avg_bps;/**< Average bitrate.			*/
    pj_uint32_t		max_bps;/**< Maximum bitrate.			*/
} pjmedia_video_format_detail;

/**
 * This macro declares the size of the detail section in #pjmedia_format
 * to be reserved for user defined detail.
 */
#ifndef PJMEDIA_FORMAT_DETAIL_USER_SIZE
#   define PJMEDIA_FORMAT_DETAIL_USER_SIZE		1
#endif

/**
 * This structure contains all the information needed to completely describe
 * a media.
 */
typedef struct pjmedia_format
{
    /**
     * The format id that specifies the audio sample or video pixel format.
     * Some well known formats ids are declared in pjmedia_format_id
     * enumeration.
     *
     * @see pjmedia_format_id
     */
    pj_uint32_t		 	 id;

    /**
     * The top-most type of the media, as an information.
     */
    pjmedia_type		 type;

    /**
     * The type of detail structure in the \a detail pointer.
     */
    pjmedia_format_detail_type	 detail_type;

    /**
     * Detail section to describe the media.
     */
    union
    {
	/**
	 * Detail section for audio format.
	 */
	pjmedia_audio_format_detail	aud;

	/**
	 * Detail section for video format.
	 */
	pjmedia_video_format_detail	vid;

	/**
	 * Reserved area for user-defined format detail.
	 */
	char				user[PJMEDIA_FORMAT_DETAIL_USER_SIZE];
    } det;

} pjmedia_format;

/**
 * This enumeration describes video color model. It mostly serves as
 * information only.
 */
typedef enum pjmedia_color_model
{
    /** The color model is unknown or unspecified. */
    PJMEDIA_COLOR_MODEL_NONE,

    /** RGB color model. */
    PJMEDIA_COLOR_MODEL_RGB,

    /** YUV color model. */
    PJMEDIA_COLOR_MODEL_YUV
} pjmedia_color_model;

/**
 * This structure holds information to apply a specific video format
 * against size and buffer information, and get additional information
 * from it. To do that, application fills up the input fields of this
 * structure, and give this structure to \a apply_fmt() function
 * of #pjmedia_video_format_info structure.
 */
typedef struct pjmedia_video_apply_fmt_param
{
    /* input fields: */

    /**
     * [IN] The image size. This field is mandatory, and has to be set
     * correctly prior to calling \a apply_fmt() function.
     */
    pjmedia_rect_size	 size;

    /**
     * [IN] Pointer to the buffer that holds the frame. The \a apply_fmt()
     * function uses this pointer to calculate the pointer for each video
     * planes of the media. This field is optional -- however, the
     * \a apply_fmt() would still fill up the \a planes[] array with the
     * correct pointer even though the buffer is set to NULL. This could be
     * useful to calculate the size (in bytes) of each plane.
     */
    pj_uint8_t		*buffer;

    /* output fields: */

    /**
     * [OUT] The size (in bytes) required of the buffer to hold the video
     * frame of the particular frame size (width, height).
     */
    pj_size_t		 framebytes;

    /**
     * [OUT] Array of strides value (in bytes) for each video plane.
     */
    int		         strides[PJMEDIA_MAX_VIDEO_PLANES];

    /**
     * [OUT] Array of pointers to each of the video planes. The values are
     * calculated from the \a buffer field.
     */
    pj_uint8_t		*planes[PJMEDIA_MAX_VIDEO_PLANES];

    /**
     * [OUT] Array of video plane sizes.
     */
    pj_size_t		 plane_bytes[PJMEDIA_MAX_VIDEO_PLANES];

} pjmedia_video_apply_fmt_param;

/**
 * This structure holds information to describe a video format. Application
 * can retrieve this structure by calling #pjmedia_get_video_format_info()
 * funcion.
 */
typedef struct pjmedia_video_format_info
{
    /**
     * The unique format ID of the media. Well known format ids are declared
     * in pjmedia_format_id enumeration.
     */
    pj_uint32_t		id;

    /**
     * Null terminated string containing short identification about the
     * format.
     */
    char		name[8];

    /**
     * Information about the color model of this video format.
     */
    pjmedia_color_model	color_model;

    /**
     * Number of bits needed to store one pixel of this video format.
     */
    pj_uint8_t		bpp;

    /**
     * Number of video planes that this format uses. Value 1 indicates
     * packed format, while value greater than 1 indicates planar format.
     */
    pj_uint8_t		plane_cnt;

    /**
     * Pointer to function to apply this format against size and buffer
     * information in pjmedia_video_apply_fmt_param argument. Application
     * uses this function to obtain various information such as the
     * memory size of a frame buffer, strides value of the image, the
     * location of the planes, and so on. See pjmedia_video_apply_fmt_param
     * for additional information.
     *
     * @param vfi	The video format info.
     * @param vafp	The parameters to investigate.
     *
     * @return		PJ_SUCCESS if the function has calculated the
     * 			information in \a vafp successfully.
     */
    pj_status_t (*apply_fmt)(const struct pjmedia_video_format_info *vfi,
	                     pjmedia_video_apply_fmt_param *vafp);

} pjmedia_video_format_info;


/*****************************************************************************
 * UTILITIES:
 */

/**
 * General utility routine to calculate samples per frame value from clock
 * rate, ptime (in usec), and channel count. Application should use this
 * macro whenever possible due to possible overflow in the math calculation.
 *
 * @param clock_rate		Clock rate.
 * @param usec_ptime		Frame interval, in microsecond.
 * @param channel_count		Number of channels.
 *
 * @return			The samples per frame value.
 */
PJ_INLINE(unsigned) PJMEDIA_SPF(unsigned clock_rate, unsigned usec_ptime,
				unsigned channel_count)
{
#if PJ_HAS_INT64
    return ((unsigned)((pj_uint64_t)usec_ptime * \
		       clock_rate * channel_count / 1000000));
#elif PJ_HAS_FLOATING_POINT
    return ((unsigned)(1.0*usec_ptime * clock_rate * channel_count / 1000000));
#else
    return ((unsigned)(usec_ptime / 1000L * clock_rate * \
		       channel_count / 1000));
#endif
}

/**
 * Variant of #PJMEDIA_SPF() which takes frame rate instead of ptime.
 */
PJ_INLINE(unsigned) PJMEDIA_SPF2(unsigned clock_rate, const pjmedia_ratio *fr,
				 unsigned channel_count)
{
#if PJ_HAS_INT64
    return ((unsigned)((pj_uint64_t)clock_rate * fr->denum \
		       / fr->num / channel_count));
#elif PJ_HAS_FLOATING_POINT
    return ((unsigned)(1.0* clock_rate * fr->denum / fr->num /channel_count));
#else
    return ((unsigned)(1L * clock_rate * fr->denum / fr->num / channel_count));
#endif
}


/**
 * Utility routine to calculate frame size (in bytes) from bitrate and frame
 * interval values. Application should use this macro whenever possible due
 * to possible overflow in the math calculation.
 *
 * @param bps			The bitrate of the stream.
 * @param usec_ptime		Frame interval, in microsecond.
 *
 * @return			Frame size in bytes.
 */
PJ_INLINE(unsigned) PJMEDIA_FSZ(unsigned bps, unsigned usec_ptime)
{
#if PJ_HAS_INT64
    return ((unsigned)((pj_uint64_t)bps * usec_ptime / PJ_UINT64(8000000)));
#elif PJ_HAS_FLOATING_POINT
    return ((unsigned)(1.0 * bps * usec_ptime / 8000000.0));
#else
    return ((unsigned)(bps / 8L * usec_ptime / 1000000));
#endif
}

/**
 * General utility routine to calculate ptime value from frame rate.
 * Application should use this macro whenever possible due to possible
 * overflow in the math calculation.
 *
 * @param frame_rate		Frame rate
 *
 * @return			The ptime value (in usec).
 */
PJ_INLINE(unsigned) PJMEDIA_PTIME(const pjmedia_ratio *frame_rate)
{
#if PJ_HAS_INT64
    return ((unsigned)((pj_uint64_t)1000000 * \
		       frame_rate->denum / frame_rate->num));
#elif PJ_HAS_FLOATING_POINT
    return ((unsigned)(1000000.0 * frame_rate->denum / \
                       frame_rate->num));
#else
    return ((unsigned)((1000L * frame_rate->denum / \
                       frame_rate->num) * 1000);
#endif
}

/**
 * Utility to retrieve samples_per_frame value from
 * pjmedia_audio_format_detail.
 *
 * @param pafd		Pointer to pjmedia_audio_format_detail
 * @return		Samples per frame
 */
PJ_INLINE(unsigned) PJMEDIA_AFD_SPF(const pjmedia_audio_format_detail *pafd)
{
    return PJMEDIA_SPF(pafd->clock_rate, pafd->frame_time_usec,
		       pafd->channel_count);
}

/**
 * Utility to retrieve average frame size from pjmedia_audio_format_detail.
 * The average frame size is derived from the average bitrate of the audio
 * stream.
 *
 * @param afd		Pointer to pjmedia_audio_format_detail
 * @return		Average frame size.
 */
PJ_INLINE(unsigned) PJMEDIA_AFD_AVG_FSZ(const pjmedia_audio_format_detail *afd)
{
    return PJMEDIA_FSZ(afd->avg_bps, afd->frame_time_usec);
}

/**
 * Utility to retrieve maximum frame size from pjmedia_audio_format_detail.
 * The maximum frame size is derived from the maximum bitrate of the audio
 * stream.
 *
 * @param afd		Pointer to pjmedia_audio_format_detail
 * @return		Average frame size.
 */
PJ_INLINE(unsigned) PJMEDIA_AFD_MAX_FSZ(const pjmedia_audio_format_detail *afd)
{
    return PJMEDIA_FSZ(afd->max_bps, afd->frame_time_usec);
}


/**
 * Initialize the format as audio format with the specified parameters.
 *
 * @param fmt			The format to be initialized.
 * @param fmt_id		Format ID. See #pjmedia_format_id
 * @param clock_rate		Audio clock rate.
 * @param channel_count		Number of channels.
 * @param bits_per_sample	Number of bits per sample.
 * @param frame_time_usec	Frame interval, in microsecond.
 * @param avg_bps		Average bitrate.
 * @param max_bps		Maximum bitrate.
 */
PJ_DECL(void) pjmedia_format_init_audio(pjmedia_format *fmt,
				        pj_uint32_t fmt_id,
					unsigned clock_rate,
					unsigned channel_count,
					unsigned bits_per_sample,
					unsigned frame_time_usec,
					pj_uint32_t avg_bps,
					pj_uint32_t max_bps);

/**
 * Initialize the format as video format with the specified parameters.
 * A format manager should have been created, as this function will need
 * to consult to a format manager in order to fill in detailed
 * information about the format.
 *
 * @param fmt		The format to be initialised.
 * @param fmt_id	Format ID. See #pjmedia_format_id
 * @param width		Image width.
 * @param height	Image heigth.
 * @param fps_num	FPS numerator.
 * @param fps_denum	FPS denumerator.
 * @param avg_bps	Average bitrate.
 * @param max_bps	Maximum bitrate.
 */
PJ_DECL(void) pjmedia_format_init_video(pjmedia_format *fmt,
					pj_uint32_t fmt_id,
					unsigned width,
					unsigned height,
					unsigned fps_num,
					unsigned fps_denum);

/**
 * Copy format to another.
 *
 * @param dst		The destination format.
 * @param src		The source format.
 *
 * @return		Pointer to destination format.
 */
PJ_DECL(pjmedia_format*) pjmedia_format_copy(pjmedia_format *dst,
					     const pjmedia_format *src);

/**
 * Check if the format contains audio format, and retrieve the audio format
 * detail in the format.
 *
 * @param fmt		The format structure.
 * @param assert_valid	If this is set to non-zero, an assertion will be
 * 			raised if the detail type is not audio or if the
 * 			the detail is NULL.
 *
 * @return		The instance of audio format detail in the format
 * 			structure, or NULL if the format doesn't contain
 * 			audio detail.
 */
PJ_DECL(pjmedia_audio_format_detail*)
pjmedia_format_get_audio_format_detail(const pjmedia_format *fmt,
				       pj_bool_t assert_valid);

/**
 * Check if the format contains video format, and retrieve the video format
 * detail in the format.
 *
 * @param fmt		The format structure.
 * @param assert_valid	If this is set to non-zero, an assertion will be
 * 			raised if the detail type is not video or if the
 * 			the detail is NULL.
 *
 * @return		The instance of video format detail in the format
 * 			structure, or NULL if the format doesn't contain
 * 			video detail.
 */
PJ_DECL(pjmedia_video_format_detail*)
pjmedia_format_get_video_format_detail(const pjmedia_format *fmt,
				       pj_bool_t assert_valid);

/*****************************************************************************
 * FORMAT MANAGEMENT:
 */

/**
 * Opaque data type for video format manager. The video format manager manages
 * the repository of video formats that the framework recognises. Typically it
 * is a singleton instance, although application may instantiate more than one
 * instances of this if required.
 */
typedef struct pjmedia_video_format_mgr pjmedia_video_format_mgr;


/**
 * Create a new video format manager instance. This will also set the pointer
 * to the singleton instance if the value is still NULL.
 *
 * @param pool		The pool to allocate memory.
 * @param max_fmt	Maximum number of formats to accommodate.
 * @param options	Option flags. Must be zero for now.
 * @param p_mgr		Pointer to hold the created instance.
 *
 * @return		PJ_SUCCESS on success, or the appripriate error value.
 */
PJ_DECL(pj_status_t)
pjmedia_video_format_mgr_create(pj_pool_t *pool,
				unsigned max_fmt,
				unsigned options,
				pjmedia_video_format_mgr **p_mgr);

/**
 * Get the singleton instance of the video format manager.
 *
 * @return		The instance.
 */
PJ_DECL(pjmedia_video_format_mgr*) pjmedia_video_format_mgr_instance(void);

/**
 * Manually assign a specific video manager instance as the singleton
 * instance. Normally this is not needed if only one instance is ever
 * going to be created, as the library automatically assign the singleton
 * instance.
 *
 * @param mgr		The instance to be used as the singleton instance.
 * 			Application may specify NULL to clear the singleton
 * 			singleton instance.
 */
PJ_DECL(void)
pjmedia_video_format_mgr_set_instance(pjmedia_video_format_mgr *mgr);

/**
 * Retrieve a video format info for the specified format id.
 *
 * @param mgr		The video format manager. Specify NULL to use
 * 			the singleton instance (however, a video format
 * 			manager still must have been created prior to
 * 			calling this function).
 * @param id		The format id which format info is to be
 * 			retrieved.
 *
 * @return		The video format info.
 */
PJ_DECL(const pjmedia_video_format_info*)
pjmedia_get_video_format_info(pjmedia_video_format_mgr *mgr,
			      pj_uint32_t id);

/**
 * Register a new video format to the framework. By default, built-in
 * formats will be registered automatically to the format manager when
 * it is created (note: built-in formats are ones which format id is
 * listed in pjmedia_format_id enumeration). This function allows
 * application to use user defined format id by registering that format
 * into the framework.
 *
 * @param mgr		The video format manager. Specify NULL to use
 * 			the singleton instance (however, a video format
 * 			manager still must have been created prior to
 * 			calling this function).
 * @param vfi		The video format info to be registered. This
 * 			structure must remain valid until the format
 * 			manager is destroyed.
 *
 * @return		PJ_SUCCESS on success, or the appripriate error value.
 */
PJ_DECL(pj_status_t)
pjmedia_register_video_format_info(pjmedia_video_format_mgr *mgr,
				   pjmedia_video_format_info *vfi);

/**
 * Destroy a video format manager. If the manager happens to be the singleton
 * instance, the singleton instance will be set to NULL.
 *
 * @param mgr		The video format manager. Specify NULL to use
 * 			the singleton instance (however, a video format
 * 			manager still must have been created prior to
 * 			calling this function).
 */
PJ_DECL(void) pjmedia_video_format_mgr_destroy(pjmedia_video_format_mgr *mgr);

PJ_END_DECL

/**
 * @}
 */

#endif	/* __PJMEDIA_FORMAT_H__ */

