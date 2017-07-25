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
 * \brief Media Format Cache API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _AST_FORMAT_CACHE_H_
#define _AST_FORMAT_CACHE_H_

struct ast_format;

/*!
 * \brief Built-in cached signed linear 8kHz format.
 */
extern struct ast_format *ast_format_slin;

/*!
 * \brief Built-in cached signed linear 12kHz format.
 */
extern struct ast_format *ast_format_slin12;

/*!
 * \brief Built-in cached signed linear 16kHz format.
 */
extern struct ast_format *ast_format_slin16;

/*!
 * \brief Built-in cached signed linear 24kHz format.
 */
extern struct ast_format *ast_format_slin24;

/*!
 * \brief Built-in cached signed linear 32kHz format.
 */
extern struct ast_format *ast_format_slin32;

/*!
 * \brief Built-in cached signed linear 44kHz format.
 */
extern struct ast_format *ast_format_slin44;

/*!
 * \brief Built-in cached signed linear 48kHz format.
 */
extern struct ast_format *ast_format_slin48;

/*!
 * \brief Built-in cached signed linear 96kHz format.
 */
extern struct ast_format *ast_format_slin96;

/*!
 * \brief Built-in cached signed linear 192kHz format.
 */
extern struct ast_format *ast_format_slin192;

/*!
 * \brief Built-in cached ulaw format.
 */
extern struct ast_format *ast_format_ulaw;

/*!
 * \brief Built-in cached alaw format.
 */
extern struct ast_format *ast_format_alaw;

/*!
 * \brief Built-in cached testlaw format.
 */
extern struct ast_format *ast_format_testlaw;

/*!
 * \brief Built-in cached gsm format.
 */
extern struct ast_format *ast_format_gsm;

/*!
 * \brief Built-in cached adpcm format.
 */
extern struct ast_format *ast_format_adpcm;

/*!
 * \brief Built-in cached g722 format.
 */
extern struct ast_format *ast_format_g722;

/*!
 * \brief Built-in cached g726 format.
 */
extern struct ast_format *ast_format_g726;

/*!
 * \brief Built-in cached g726 aal2 format.
 */
extern struct ast_format *ast_format_g726_aal2;

/*!
 * \brief Built-in cached ilbc format.
 */
extern struct ast_format *ast_format_ilbc;

/*!
 * \brief Built-in cached ilbc format.
 */
extern struct ast_format *ast_format_lpc10;

/*!
 * \brief Built-in cached speex format.
 */
extern struct ast_format *ast_format_speex;

/*!
 * \brief Built-in cached speex at 16kHz format.
 */
extern struct ast_format *ast_format_speex16;

/*!
 * \brief Built-in cached speex at 32kHz format.
 */
extern struct ast_format *ast_format_speex32;

/*!
 * \brief Built-in cached g723.1 format.
 */
extern struct ast_format *ast_format_g723;

/*!
 * \brief Built-in cached g729 format.
 */
extern struct ast_format *ast_format_g729;

/*!
 * \brief Built-in cached g719 format.
 */
extern struct ast_format *ast_format_g719;

/*!
 * \brief Built-in cached h261 format.
 */
extern struct ast_format *ast_format_h261;

/*!
 * \brief Built-in cached h263 format.
 */
extern struct ast_format *ast_format_h263;

/*!
 * \brief Built-in cached h263 plus format.
 */
extern struct ast_format *ast_format_h263p;

/*!
 * \brief Built-in cached h264 format.
 */
extern struct ast_format *ast_format_h264;

/*!
 * \brief Built-in cached mp4 format.
 */
extern struct ast_format *ast_format_mp4;

/*!
 * \brief Built-in cached vp8 format.
 */
extern struct ast_format *ast_format_vp8;

/*!
 * \brief Built-in cached vp9 format.
 */
extern struct ast_format *ast_format_vp9;

/*!
 * \brief Built-in cached jpeg format.
 */
extern struct ast_format *ast_format_jpeg;

/*!
 * \brief Built-in cached png format.
 */
extern struct ast_format *ast_format_png;

/*!
 * \brief Built-in cached siren14 format.
 */
extern struct ast_format *ast_format_siren14;

/*!
 * \brief Built-in cached siren7 format.
 */
extern struct ast_format *ast_format_siren7;

/*!
 * \brief Built-in cached opus format.
 */
extern struct ast_format *ast_format_opus;

/*!
 * \brief Built-in cached Codec 2 format.
 */
extern struct ast_format *ast_format_codec2;

/*!
 * \brief Built-in cached t140 format.
 */
extern struct ast_format *ast_format_t140;

/*!
 * \brief Built-in cached t140 red format.
 */
extern struct ast_format *ast_format_t140_red;

/*!
 * \brief Built-in cached T.38 format.
 */
extern struct ast_format *ast_format_t38;

/*!
 * \brief Built-in "null" format.
 */
extern struct ast_format *ast_format_none;

/*!
 * \brief Built-in SILK format.
 */
extern struct ast_format *ast_format_silk8;
extern struct ast_format *ast_format_silk12;
extern struct ast_format *ast_format_silk16;
extern struct ast_format *ast_format_silk24;

/*!
 * \brief Initialize format cache support within the core.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_format_cache_init(void);

/*!
 * \brief Set a named format cache entry.
 *
 * \param format A pointer to the format to cache
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_format_cache_set(struct ast_format *format);

/*!
 * \brief Retrieve a named format from the cache.
 *
 * \param name Name of the cached format
 *
 * \retval non-NULL if found
 * \retval NULL if not found
 *
 * \note The returned format has its reference count incremented. It must be
 * dropped using ao2_ref or ao2_cleanup.
 */
struct ast_format *__ast_format_cache_get(const char *name,
	const char *tag, const char *file, int line, const char *func);

#define ast_format_cache_get(name) \
	__ast_format_cache_get((name), "ast_format_cache_get", __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ast_t_format_cache_get(name, tag) \
	__ast_format_cache_get((name), (tag), __FILE__, __LINE__, __PRETTY_FUNCTION__)


/*!
 * \brief Retrieve the best signed linear format given a sample rate.
 *
 * \param rate The sample rate
 *
 * \details
 * This is a convenience function that returns one of the global
 * ast_format_slinxxx formats.
 *
 * \return pointer to the signed linear format
 *
 * \note The returned format has NOT had its reference count incremented.
 */
struct ast_format *ast_format_cache_get_slin_by_rate(unsigned int rate);

/*!
 * \brief Determines if a format is one of the cached slin formats
 *
 * \param format The format to check
 *
 * \retval 0 if the format is not an SLIN format
 * \retval 1 if the format is an SLIN format
 */
int ast_format_cache_is_slinear(struct ast_format *format);

#endif /* _AST_FORMAT_CACHE_H */
