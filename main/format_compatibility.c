/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Media Format Bitfield Compatibility API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/codec.h"
#include "asterisk/format.h"
#include "asterisk/format_compatibility.h"
#include "asterisk/format_cache.h"

uint64_t ast_format_compatibility_format2bitfield(const struct ast_format *format)
{
	if (ast_format_cmp(format, ast_format_g723) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_G723;
	} else if (ast_format_cmp(format, ast_format_gsm) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_GSM;
	} else if (ast_format_cmp(format, ast_format_ulaw) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_ULAW;
	} else if (ast_format_cmp(format, ast_format_alaw) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_ALAW;
	} else if (ast_format_cmp(format, ast_format_g726_aal2) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_G726_AAL2;
	} else if (ast_format_cmp(format, ast_format_adpcm) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_ADPCM;
	} else if (ast_format_cmp(format, ast_format_slin) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_SLIN;
	} else if (ast_format_cmp(format, ast_format_lpc10) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_LPC10;
	} else if (ast_format_cmp(format, ast_format_g729) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_G729;
	} else if (ast_format_cmp(format, ast_format_speex) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_SPEEX;
	} else if (ast_format_cmp(format, ast_format_ilbc) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_ILBC;
	} else if (ast_format_cmp(format, ast_format_g726) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_G726;
	} else if (ast_format_cmp(format, ast_format_g722) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_G722;
	} else if (ast_format_cmp(format, ast_format_siren7) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_SIREN7;
	} else if (ast_format_cmp(format, ast_format_siren14) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_SIREN14;
	} else if (ast_format_cmp(format, ast_format_slin16) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_SLIN16;
	} else if (ast_format_cmp(format, ast_format_g719) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_G719;
	} else if (ast_format_cmp(format, ast_format_speex16) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_SPEEX16;
	} else if (ast_format_cmp(format, ast_format_opus) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_OPUS;
	} else if (ast_format_cmp(format, ast_format_testlaw) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_TESTLAW;
	} else if (ast_format_cmp(format, ast_format_h261) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_H261;
	} else if (ast_format_cmp(format, ast_format_h263) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_H263;
	} else if (ast_format_cmp(format, ast_format_h263p) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_H263P;
	} else if (ast_format_cmp(format, ast_format_h264) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_H264;
	} else if (ast_format_cmp(format, ast_format_mp4) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_MP4;
	} else if (ast_format_cmp(format, ast_format_vp8) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_VP8;
	} else if (ast_format_cmp(format, ast_format_jpeg) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_JPEG;
	} else if (ast_format_cmp(format, ast_format_png) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_PNG;
	} else if (ast_format_cmp(format, ast_format_t140_red) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_T140_RED;
	} else if (ast_format_cmp(format, ast_format_t140) == AST_FORMAT_CMP_EQUAL) {
		return AST_FORMAT_T140;
	}

	return 0;
}

uint64_t ast_format_compatibility_codec2bitfield(const struct ast_codec *codec)
{
	if (codec->id == ast_format_get_codec_id(ast_format_g723)) {
		return AST_FORMAT_G723;
	} else if (codec->id == ast_format_get_codec_id(ast_format_gsm)) {
		return AST_FORMAT_GSM;
	} else if (codec->id == ast_format_get_codec_id(ast_format_ulaw)) {
		return AST_FORMAT_ULAW;
	} else if (codec->id == ast_format_get_codec_id(ast_format_alaw)) {
		return AST_FORMAT_ALAW;
	} else if (codec->id == ast_format_get_codec_id(ast_format_g726_aal2)) {
		return AST_FORMAT_G726_AAL2;
	} else if (codec->id == ast_format_get_codec_id(ast_format_adpcm)) {
		return AST_FORMAT_ADPCM;
	} else if (codec->id == ast_format_get_codec_id(ast_format_slin)) {
		return AST_FORMAT_SLIN;
	} else if (codec->id == ast_format_get_codec_id(ast_format_lpc10)) {
		return AST_FORMAT_LPC10;
	} else if (codec->id == ast_format_get_codec_id(ast_format_g729)) {
		return AST_FORMAT_G729;
	} else if (codec->id == ast_format_get_codec_id(ast_format_speex)) {
		return AST_FORMAT_SPEEX;
	} else if (codec->id == ast_format_get_codec_id(ast_format_ilbc)) {
		return AST_FORMAT_ILBC;
	} else if (codec->id == ast_format_get_codec_id(ast_format_g726)) {
		return AST_FORMAT_G726;
	} else if (codec->id == ast_format_get_codec_id(ast_format_g722)) {
		return AST_FORMAT_G722;
	} else if (codec->id == ast_format_get_codec_id(ast_format_siren7)) {
		return AST_FORMAT_SIREN7;
	} else if (codec->id == ast_format_get_codec_id(ast_format_siren14)) {
		return AST_FORMAT_SIREN14;
	} else if (codec->id == ast_format_get_codec_id(ast_format_slin16)) {
		return AST_FORMAT_SLIN16;
	} else if (codec->id == ast_format_get_codec_id(ast_format_g719)) {
		return AST_FORMAT_G719;
	} else if (codec->id == ast_format_get_codec_id(ast_format_speex16)) {
		return AST_FORMAT_SPEEX16;
	} else if (codec->id == ast_format_get_codec_id(ast_format_opus)) {
		return AST_FORMAT_OPUS;
	} else if (codec->id == ast_format_get_codec_id(ast_format_testlaw)) {
		return AST_FORMAT_TESTLAW;
	} else if (codec->id == ast_format_get_codec_id(ast_format_h261)) {
		return AST_FORMAT_H261;
	} else if (codec->id == ast_format_get_codec_id(ast_format_h263)) {
		return AST_FORMAT_H263;
	} else if (codec->id == ast_format_get_codec_id(ast_format_h263p)) {
		return AST_FORMAT_H263P;
	} else if (codec->id == ast_format_get_codec_id(ast_format_h264)) {
		return AST_FORMAT_H264;
	} else if (codec->id == ast_format_get_codec_id(ast_format_mp4)) {
		return AST_FORMAT_MP4;
	} else if (codec->id == ast_format_get_codec_id(ast_format_vp8)) {
		return AST_FORMAT_VP8;
	} else if (codec->id == ast_format_get_codec_id(ast_format_jpeg)) {
		return AST_FORMAT_JPEG;
	} else if (codec->id == ast_format_get_codec_id(ast_format_png)) {
		return AST_FORMAT_PNG;
	} else if (codec->id == ast_format_get_codec_id(ast_format_t140_red)) {
		return AST_FORMAT_T140_RED;
	} else if (codec->id == ast_format_get_codec_id(ast_format_t140)) {
		return AST_FORMAT_T140;
	}

	return 0;
}

struct ast_format *ast_format_compatibility_bitfield2format(uint64_t bitfield)
{
	switch (bitfield) {
	/*! G.723.1 compression */
	case AST_FORMAT_G723:
		return ast_format_g723;
	/*! GSM compression */
	case AST_FORMAT_GSM:
		return ast_format_gsm;
	/*! Raw mu-law data (G.711) */
	case AST_FORMAT_ULAW:
		return ast_format_ulaw;
	/*! Raw A-law data (G.711) */
	case AST_FORMAT_ALAW:
		return ast_format_alaw;
	/*! ADPCM (G.726, 32kbps, AAL2 codeword packing) */
	case AST_FORMAT_G726_AAL2:
		return ast_format_g726_aal2;
	/*! ADPCM (IMA) */
	case AST_FORMAT_ADPCM:
		return ast_format_adpcm;
	/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
	case AST_FORMAT_SLIN:
		return ast_format_slin;
	/*! LPC10, 180 samples/frame */
	case AST_FORMAT_LPC10:
		return ast_format_lpc10;
	/*! G.729A audio */
	case AST_FORMAT_G729:
		return ast_format_g729;
	/*! SpeeX Free Compression */
	case AST_FORMAT_SPEEX:
		return ast_format_speex;
	/*! iLBC Free Compression */
	case AST_FORMAT_ILBC:
		return ast_format_ilbc;
	/*! ADPCM (G.726, 32kbps, RFC3551 codeword packing) */
	case AST_FORMAT_G726:
		return ast_format_g726;
	/*! G.722 */
	case AST_FORMAT_G722:
		return ast_format_g722;
	/*! G.722.1 (also known as Siren7, 32kbps assumed) */
	case AST_FORMAT_SIREN7:
		return ast_format_siren7;
	/*! G.722.1 Annex C (also known as Siren14, 48kbps assumed) */
	case AST_FORMAT_SIREN14:
		return ast_format_siren14;
	/*! Raw 16-bit Signed Linear (16000 Hz) PCM */
	case AST_FORMAT_SLIN16:
		return ast_format_slin16;
	/*! G.719 (64 kbps assumed) */
	case AST_FORMAT_G719:
		return ast_format_g719;
	/*! SpeeX Wideband (16kHz) Free Compression */
	case AST_FORMAT_SPEEX16:
		return ast_format_speex16;
	/*! Opus audio (8kHz, 16kHz, 24kHz, 48Khz) */
	case AST_FORMAT_OPUS:
		return ast_format_opus;
	/*! Raw mu-law data (G.711) */
	case AST_FORMAT_TESTLAW:
		return ast_format_testlaw;

	/*! H.261 Video */
	case AST_FORMAT_H261:
		return ast_format_h261;
	/*! H.263 Video */
	case AST_FORMAT_H263:
		return ast_format_h263;
	/*! H.263+ Video */
	case AST_FORMAT_H263P:
		return ast_format_h263p;
	/*! H.264 Video */
	case AST_FORMAT_H264:
		return ast_format_h264;
	/*! MPEG4 Video */
	case AST_FORMAT_MP4:
		return ast_format_mp4;
	/*! VP8 Video */
	case AST_FORMAT_VP8:
		return ast_format_vp8;

	/*! JPEG Images */
	case AST_FORMAT_JPEG:
		return ast_format_jpeg;
	/*! PNG Images */
	case AST_FORMAT_PNG:
		return ast_format_png;

	/*! T.140 RED Text format RFC 4103 */
	case AST_FORMAT_T140_RED:
		return ast_format_t140_red;
	/*! T.140 Text format - ITU T.140, RFC 4103 */
	case AST_FORMAT_T140:
		return ast_format_t140;
	}
	return NULL;
}
