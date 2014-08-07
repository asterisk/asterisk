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

/*!
 * \file
 * \brief Media Format Bitfield Compatibility API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _AST_FORMAT_COMPATIBILITY_H_
#define _AST_FORMAT_COMPATIBILITY_H_

struct ast_format;
struct ast_codec;

/*
 * Legacy bitfields for specific formats
 */

/*! G.723.1 compression */
#define AST_FORMAT_G723 (1ULL << 0)
/*! GSM compression */
#define AST_FORMAT_GSM (1ULL << 1)
/*! Raw mu-law data (G.711) */
#define AST_FORMAT_ULAW (1ULL << 2)
/*! Raw A-law data (G.711) */
#define AST_FORMAT_ALAW (1ULL << 3)
/*! ADPCM (G.726, 32kbps, AAL2 codeword packing) */
#define AST_FORMAT_G726_AAL2 (1ULL << 4)
/*! ADPCM (IMA) */
#define AST_FORMAT_ADPCM (1ULL << 5)
/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
#define AST_FORMAT_SLIN (1ULL << 6)
/*! LPC10, 180 samples/frame */
#define AST_FORMAT_LPC10 (1ULL << 7)
/*! G.729A audio */
#define AST_FORMAT_G729 (1ULL << 8)
/*! SpeeX Free Compression */
#define AST_FORMAT_SPEEX (1ULL << 9)
/*! iLBC Free Compression */
#define AST_FORMAT_ILBC (1ULL << 10)
/*! ADPCM (G.726, 32kbps, RFC3551 codeword packing) */
#define AST_FORMAT_G726 (1ULL << 11)
/*! G.722 */
#define AST_FORMAT_G722 (1ULL << 12)
/*! G.722.1 (also known as Siren7, 32kbps assumed) */
#define AST_FORMAT_SIREN7 (1ULL << 13)
/*! G.722.1 Annex C (also known as Siren14, 48kbps assumed) */
#define AST_FORMAT_SIREN14 (1ULL << 14)
/*! Raw 16-bit Signed Linear (16000 Hz) PCM */
#define AST_FORMAT_SLIN16 (1ULL << 15)
/*! G.719 (64 kbps assumed) */
#define AST_FORMAT_G719 (1ULL << 32)
/*! SpeeX Wideband (16kHz) Free Compression */
#define AST_FORMAT_SPEEX16 (1ULL << 33)
/*! Opus audio (8kHz, 16kHz, 24kHz, 48Khz) */
#define AST_FORMAT_OPUS (1ULL << 34)
/*! Raw testing-law data (G.711) */
#define AST_FORMAT_TESTLAW (1ULL << 47)
/*! H.261 Video */
#define AST_FORMAT_H261 (1ULL << 18)
/*! H.263 Video */
#define AST_FORMAT_H263 (1ULL << 19)
/*! H.263+ Video */
#define AST_FORMAT_H263P (1ULL << 20)
/*! H.264 Video */
#define AST_FORMAT_H264 (1ULL << 21)
/*! MPEG4 Video */
#define AST_FORMAT_MP4 (1ULL << 22)
/*! VP8 Video */
#define AST_FORMAT_VP8 (1ULL << 23)
/*! JPEG Images */
#define AST_FORMAT_JPEG (1ULL << 16)
/*! PNG Images */
#define AST_FORMAT_PNG (1ULL << 17)
/*! T.140 RED Text format RFC 4103 */
#define AST_FORMAT_T140_RED (1ULL << 26)
/*! T.140 Text format - ITU T.140, RFC 4103 */
#define AST_FORMAT_T140 (1ULL << 27)

/*!
 * \brief Convert a format structure to its respective bitfield
 *
 * \param format The media format
 *
 * \retval non-zero success
 * \retval zero format not supported
 */
uint64_t ast_format_compatibility_format2bitfield(const struct ast_format *format);

/*!
 * \brief Convert a codec structure to its respective bitfield
 *
 * \param codec The media codec
 *
 * \retval non-zero success
 * \retval zero format not supported
 */
uint64_t ast_format_compatibility_codec2bitfield(const struct ast_codec *codec);

/*!
 * \brief Convert a bitfield to its respective format structure
 *
 * \param bitfield The bitfield for the media format
 *
 * \retval non-NULL success
 * \retval NULL failure (The format bitfield value is not supported)
 *
 * \note The reference count of the returned format is NOT incremented
 */
struct ast_format *ast_format_compatibility_bitfield2format(uint64_t bitfield);

#endif /* _AST_FORMAT_COMPATIBILITY_H */
