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
#include <pjmedia-codec.h>
#include <pjmedia/g711.h>

PJ_DEF(void) pjmedia_audio_codec_config_default(pjmedia_audio_codec_config*cfg)
{
    pj_bzero(cfg, sizeof(*cfg));
    cfg->speex.option = 0;
    cfg->speex.quality = PJMEDIA_CODEC_SPEEX_DEFAULT_QUALITY;
    cfg->speex.complexity = PJMEDIA_CODEC_SPEEX_DEFAULT_COMPLEXITY;
    cfg->ilbc.mode = 30;
    cfg->passthrough.setting.ilbc_mode = cfg->ilbc.mode;
}

PJ_DEF(pj_status_t)
pjmedia_codec_register_audio_codecs(pjmedia_endpt *endpt,
                                    const pjmedia_audio_codec_config *c)
{
    pjmedia_audio_codec_config default_cfg;
    pj_status_t status;

    PJ_ASSERT_RETURN(endpt, PJ_EINVAL);
    if (!c) {
	pjmedia_audio_codec_config_default(&default_cfg);
	c = &default_cfg;
    }

    PJ_ASSERT_RETURN(c->ilbc.mode==20 || c->ilbc.mode==30, PJ_EINVAL);

#if PJMEDIA_HAS_PASSTHROUGH_CODECS
    status = pjmedia_codec_passthrough_init2(endpt, &c->passthrough.setting);
    if (status != PJ_SUCCESS)
	return status;
#endif

#if PJMEDIA_HAS_SPEEX_CODEC
    /* Register speex. */
    status = pjmedia_codec_speex_init(endpt, c->speex.option,
				      c->speex.quality,
				      c->speex.complexity);
    if (status != PJ_SUCCESS)
	return status;
#endif

#if PJMEDIA_HAS_ILBC_CODEC
    /* Register iLBC. */
    status = pjmedia_codec_ilbc_init( endpt, c->ilbc.mode);
    if (status != PJ_SUCCESS)
	return status;
#endif /* PJMEDIA_HAS_ILBC_CODEC */

#if PJMEDIA_HAS_GSM_CODEC
    /* Register GSM */
    status = pjmedia_codec_gsm_init(endpt);
    if (status != PJ_SUCCESS)
	return status;
#endif /* PJMEDIA_HAS_GSM_CODEC */

#if PJMEDIA_HAS_G711_CODEC
    /* Register PCMA and PCMU */
    status = pjmedia_codec_g711_init(endpt);
    if (status != PJ_SUCCESS)
	return status;
#endif	/* PJMEDIA_HAS_G711_CODEC */

#if PJMEDIA_HAS_G722_CODEC
    status = pjmedia_codec_g722_init(endpt );
    if (status != PJ_SUCCESS)
	return status;
#endif  /* PJMEDIA_HAS_G722_CODEC */

#if PJMEDIA_HAS_INTEL_IPP
    /* Register IPP codecs */
    status = pjmedia_codec_ipp_init(endpt);
    if (status != PJ_SUCCESS)
	return status;
#endif /* PJMEDIA_HAS_INTEL_IPP */

#if PJMEDIA_HAS_G7221_CODEC
    /* Register G722.1 codecs */
    status = pjmedia_codec_g7221_init(endpt);
    if (status != PJ_SUCCESS)
	return status;
#endif /* PJMEDIA_HAS_G7221_CODEC */

#if PJMEDIA_HAS_L16_CODEC
    /* Register L16 family codecs */
    status = pjmedia_codec_l16_init(endpt, 0);
    if (status != PJ_SUCCESS)
	return status;
#endif	/* PJMEDIA_HAS_L16_CODEC */

#if PJMEDIA_HAS_OPENCORE_AMRNB_CODEC
    /* Register OpenCORE AMR-NB */
    status = pjmedia_codec_opencore_amrnb_init(endpt);
    if (status != PJ_SUCCESS)
	return status;
#endif

    return PJ_SUCCESS;
}

