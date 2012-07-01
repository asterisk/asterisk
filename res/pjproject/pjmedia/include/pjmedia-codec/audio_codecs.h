/* $Id$ */
/* 
 * Copyright (C) 2011-2011 Teluu Inc. (http://www.teluu.com)
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
#ifndef __PJMEDIA_CODEC_ALL_CODECS_H__
#define __PJMEDIA_CODEC_ALL_CODECS_H__

/**
 * @file pjmedia-codec/all_codecs.h
 * @brief Helper function to register all codecs
 */
#include <pjmedia/endpoint.h>
#include <pjmedia-codec/passthrough.h>


PJ_BEGIN_DECL

/**
 * @defgroup PJMEDIA_CODEC_REGISTER_ALL Codec registration helper
 * @ingroup PJMEDIA_CODEC_CODECS
 * @brief Helper function to register all codecs
 * @{
 *
 * Helper function to register all codecs that are implemented in
 * PJMEDIA-CODEC library.
 */

/**
 * Codec configuration. Call #pjmedia_audio_codec_config_default() to initialize
 * this structure with the default values.
 */
typedef struct pjmedia_audio_codec_config
{
    /** Speex codec settings. See #pjmedia_codec_speex_init() for more info */
    struct {
	unsigned	option;		/**< Bitmask of options.	*/
	int		quality;	/**< Codec quality.		*/
	int		complexity;	/**< Codec complexity.		*/
    } speex;

    /** iLBC settings */
    struct {
	unsigned	mode;		/**< iLBC mode.			*/
    } ilbc;

    /** Passthrough */
    struct {
	pjmedia_codec_passthrough_setting setting; /**< Passthrough	*/
    } passthrough;

} pjmedia_audio_codec_config;


/**
 * Initialize pjmedia_audio_codec_config structure with default values.
 *
 * @param cfg		The codec config to be initialized.
 */
PJ_DECL(void)
pjmedia_audio_codec_config_default(pjmedia_audio_codec_config *cfg);

/**
 * Register all known audio codecs implemented in PJMEDA-CODEC library to the
 * specified media endpoint.
 *
 * @param endpt		The media endpoint.
 * @param c		Optional codec configuration, or NULL to use default
 * 			values.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t)
pjmedia_codec_register_audio_codecs(pjmedia_endpt *endpt,
                                    const pjmedia_audio_codec_config *c);


/**
 * @}  PJMEDIA_CODEC_REGISTER_ALL
 */


PJ_END_DECL

#endif	/* __PJMEDIA_CODEC_ALL_CODECS_H__ */
