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
 * \brief Media Format Cache API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/logger.h"
#include "asterisk/format.h"
#include "asterisk/format_cache.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"

/*!
 * \brief Built-in cached signed linear 8kHz format.
 */
struct ast_format *ast_format_slin;

/*!
 * \brief Built-in cached signed linear 12kHz format.
 */
struct ast_format *ast_format_slin12;

/*!
 * \brief Built-in cached signed linear 16kHz format.
 */
struct ast_format *ast_format_slin16;

/*!
 * \brief Built-in cached signed linear 24kHz format.
 */
struct ast_format *ast_format_slin24;

/*!
 * \brief Built-in cached signed linear 32kHz format.
 */
struct ast_format *ast_format_slin32;

/*!
 * \brief Built-in cached signed linear 44kHz format.
 */
struct ast_format *ast_format_slin44;

/*!
 * \brief Built-in cached signed linear 48kHz format.
 */
struct ast_format *ast_format_slin48;

/*!
 * \brief Built-in cached signed linear 96kHz format.
 */
struct ast_format *ast_format_slin96;

/*!
 * \brief Built-in cached signed linear 192kHz format.
 */
struct ast_format *ast_format_slin192;

/*!
 * \brief Built-in cached ulaw format.
 */
struct ast_format *ast_format_ulaw;

/*!
 * \brief Built-in cached alaw format.
 */
struct ast_format *ast_format_alaw;

/*!
 * \brief Built-in cached testlaw format.
 */
struct ast_format *ast_format_testlaw;

/*!
 * \brief Built-in cached gsm format.
 */
struct ast_format *ast_format_gsm;

/*!
 * \brief Built-in cached adpcm format.
 */
struct ast_format *ast_format_adpcm;

/*!
 * \brief Built-in cached g722 format.
 */
struct ast_format *ast_format_g722;

/*!
 * \brief Built-in cached g726 format.
 */
struct ast_format *ast_format_g726;

/*!
 * \brief Built-in cached g726-aal2 format.
 */
struct ast_format *ast_format_g726_aal2;

/*!
 * \brief Built-in cached ilbc format.
 */
struct ast_format *ast_format_ilbc;

/*!
 * \brief Built-in cached ilbc format.
 */
struct ast_format *ast_format_lpc10;

/*!
 * \brief Built-in cached speex format.
 */
struct ast_format *ast_format_speex;

/*!
 * \brief Built-in cached speex at 16kHz format.
 */
struct ast_format *ast_format_speex16;

/*!
 * \brief Built-in cached speex at 32kHz format.
 */
struct ast_format *ast_format_speex32;

/*!
 * \brief Built-in cached g723.1 format.
 */
struct ast_format *ast_format_g723;

/*!
 * \brief Built-in cached g729 format.
 */
struct ast_format *ast_format_g729;

/*!
 * \brief Built-in cached g719 format.
 */
struct ast_format *ast_format_g719;

/*!
 * \brief Built-in cached h261 format.
 */
struct ast_format *ast_format_h261;

/*!
 * \brief Built-in cached h263 format.
 */
struct ast_format *ast_format_h263;

/*!
 * \brief Built-in cached h263 plus format.
 */
struct ast_format *ast_format_h263p;

/*!
 * \brief Built-in cached h264 format.
 */
struct ast_format *ast_format_h264;

/*!
 * \brief Built-in cached h265 format.
 */
struct ast_format *ast_format_h265;

/*!
 * \brief Built-in cached mp4 format.
 */
struct ast_format *ast_format_mp4;

/*!
 * \brief Built-in cached vp8 format.
 */
struct ast_format *ast_format_vp8;

/*!
 * \brief Built-in cached vp9 format.
 */
struct ast_format *ast_format_vp9;

/*!
 * \brief Built-in cached jpeg format.
 */
struct ast_format *ast_format_jpeg;

/*!
 * \brief Built-in cached png format.
 */
struct ast_format *ast_format_png;

/*!
 * \brief Built-in cached siren14 format.
 */
struct ast_format *ast_format_siren14;

/*!
 * \brief Built-in cached siren7 format.
 */
struct ast_format *ast_format_siren7;

/*!
 * \brief Built-in cached opus format.
 */
struct ast_format *ast_format_opus;

/*!
 * \brief Built-in cached codec2 format.
 */
struct ast_format *ast_format_codec2;

/*!
 * \brief Built-in cached t140 format.
 */
struct ast_format *ast_format_t140;

/*!
 * \brief Built-in cached t140 red format.
 */
struct ast_format *ast_format_t140_red;

/*!
 * \brief Built-in cached T.38 format.
 */
struct ast_format *ast_format_t38;

/*!
 * \brief Built-in "null" format.
 */
struct ast_format *ast_format_none;

/*!
 * \brief Built-in "silk" format
 */
struct ast_format *ast_format_silk8;
struct ast_format *ast_format_silk12;
struct ast_format *ast_format_silk16;
struct ast_format *ast_format_silk24;

/*! \brief Number of buckets to use for the media format cache (should be prime for performance reasons) */
#define CACHE_BUCKETS 53

/*! \brief Cached formats */
static struct ao2_container *formats;

static int format_hash_cb(const void *obj, int flags)
{
	const struct ast_format *format;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		return ast_str_case_hash(key);
	case OBJ_SEARCH_OBJECT:
		format = obj;
		return ast_str_case_hash(ast_format_get_name(format));
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
}

static int format_cmp_cb(void *obj, void *arg, int flags)
{
	const struct ast_format *left = obj;
	const struct ast_format *right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = ast_format_get_name(right);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcasecmp(ast_format_get_name(left), right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncasecmp(ast_format_get_name(left), right_key, strlen(right_key));
		break;
	default:
		ast_assert(0);
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}

	return CMP_MATCH;
}

/*! \brief Function called when the process is shutting down */
static void format_cache_shutdown(void)
{
	ao2_cleanup(formats);
	formats = NULL;

	ao2_replace(ast_format_g723, NULL);
	ao2_replace(ast_format_ulaw, NULL);
	ao2_replace(ast_format_alaw, NULL);
	ao2_replace(ast_format_gsm, NULL);
	ao2_replace(ast_format_g726, NULL);
	ao2_replace(ast_format_g726_aal2, NULL);
	ao2_replace(ast_format_adpcm, NULL);
	ao2_replace(ast_format_slin, NULL);
	ao2_replace(ast_format_slin12, NULL);
	ao2_replace(ast_format_slin16, NULL);
	ao2_replace(ast_format_slin24, NULL);
	ao2_replace(ast_format_slin32, NULL);
	ao2_replace(ast_format_slin44, NULL);
	ao2_replace(ast_format_slin48, NULL);
	ao2_replace(ast_format_slin96, NULL);
	ao2_replace(ast_format_slin192, NULL);
	ao2_replace(ast_format_lpc10, NULL);
	ao2_replace(ast_format_g729, NULL);
	ao2_replace(ast_format_speex, NULL);
	ao2_replace(ast_format_speex16, NULL);
	ao2_replace(ast_format_speex32, NULL);
	ao2_replace(ast_format_ilbc, NULL);
	ao2_replace(ast_format_g722, NULL);
	ao2_replace(ast_format_siren7, NULL);
	ao2_replace(ast_format_siren14, NULL);
	ao2_replace(ast_format_testlaw, NULL);
	ao2_replace(ast_format_g719, NULL);
	ao2_replace(ast_format_opus, NULL);
	ao2_replace(ast_format_codec2, NULL);
	ao2_replace(ast_format_jpeg, NULL);
	ao2_replace(ast_format_png, NULL);
	ao2_replace(ast_format_h261, NULL);
	ao2_replace(ast_format_h263, NULL);
	ao2_replace(ast_format_h263p, NULL);
	ao2_replace(ast_format_h264, NULL);
	ao2_replace(ast_format_h265, NULL);
	ao2_replace(ast_format_mp4, NULL);
	ao2_replace(ast_format_vp8, NULL);
	ao2_replace(ast_format_vp9, NULL);
	ao2_replace(ast_format_t140_red, NULL);
	ao2_replace(ast_format_t140, NULL);
	ao2_replace(ast_format_t38, NULL);
	ao2_replace(ast_format_none, NULL);
	ao2_replace(ast_format_silk8, NULL);
	ao2_replace(ast_format_silk12, NULL);
	ao2_replace(ast_format_silk16, NULL);
	ao2_replace(ast_format_silk24, NULL);
}

int ast_format_cache_init(void)
{
	formats = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK, 0, CACHE_BUCKETS,
		format_hash_cb, NULL, format_cmp_cb);
	if (!formats) {
		return -1;
	}

	ast_register_cleanup(format_cache_shutdown);

	return 0;
}

static void set_cached_format(const char *name, struct ast_format *format)
{
	if (!strcmp(name, "codec2")) {
		ao2_replace(ast_format_codec2, format);
	} else if (!strcmp(name, "g723")) {
		ao2_replace(ast_format_g723, format);
	} else if (!strcmp(name, "ulaw")) {
		ao2_replace(ast_format_ulaw, format);
	} else if (!strcmp(name, "alaw")) {
		ao2_replace(ast_format_alaw, format);
	} else if (!strcmp(name, "gsm")) {
		ao2_replace(ast_format_gsm, format);
	} else if (!strcmp(name, "g726")) {
		ao2_replace(ast_format_g726, format);
	} else if (!strcmp(name, "g726aal2")) {
		ao2_replace(ast_format_g726_aal2, format);
	} else if (!strcmp(name, "adpcm")) {
		ao2_replace(ast_format_adpcm, format);
	} else if (!strcmp(name, "slin")) {
		ao2_replace(ast_format_slin, format);
	} else if (!strcmp(name, "slin12")) {
		ao2_replace(ast_format_slin12, format);
	} else if (!strcmp(name, "slin16")) {
		ao2_replace(ast_format_slin16, format);
	} else if (!strcmp(name, "slin24")) {
		ao2_replace(ast_format_slin24, format);
	} else if (!strcmp(name, "slin32")) {
		ao2_replace(ast_format_slin32, format);
	} else if (!strcmp(name, "slin44")) {
		ao2_replace(ast_format_slin44, format);
	} else if (!strcmp(name, "slin48")) {
		ao2_replace(ast_format_slin48, format);
	} else if (!strcmp(name, "slin96")) {
		ao2_replace(ast_format_slin96, format);
	} else if (!strcmp(name, "slin192")) {
		ao2_replace(ast_format_slin192, format);
	} else if (!strcmp(name, "lpc10")) {
		ao2_replace(ast_format_lpc10, format);
	} else if (!strcmp(name, "g729")) {
		ao2_replace(ast_format_g729, format);
	} else if (!strcmp(name, "speex")) {
		ao2_replace(ast_format_speex, format);
	} else if (!strcmp(name, "speex16")) {
		ao2_replace(ast_format_speex16, format);
	} else if (!strcmp(name, "speex32")) {
		ao2_replace(ast_format_speex32, format);
	} else if (!strcmp(name, "ilbc")) {
		ao2_replace(ast_format_ilbc, format);
	} else if (!strcmp(name, "g722")) {
		ao2_replace(ast_format_g722, format);
	} else if (!strcmp(name, "siren7")) {
		ao2_replace(ast_format_siren7, format);
	} else if (!strcmp(name, "siren14")) {
		ao2_replace(ast_format_siren14, format);
	} else if (!strcmp(name, "testlaw")) {
		ao2_replace(ast_format_testlaw, format);
	} else if (!strcmp(name, "g719")) {
		ao2_replace(ast_format_g719, format);
	} else if (!strcmp(name, "opus")) {
		ao2_replace(ast_format_opus, format);
	} else if (!strcmp(name, "jpeg")) {
		ao2_replace(ast_format_jpeg, format);
	} else if (!strcmp(name, "png")) {
		ao2_replace(ast_format_png, format);
	} else if (!strcmp(name, "h261")) {
		ao2_replace(ast_format_h261, format);
	} else if (!strcmp(name, "h263")) {
		ao2_replace(ast_format_h263, format);
	} else if (!strcmp(name, "h263p")) {
		ao2_replace(ast_format_h263p, format);
	} else if (!strcmp(name, "h264")) {
		ao2_replace(ast_format_h264, format);
	} else if (!strcmp(name, "h265")) {
		ao2_replace(ast_format_h265, format);
	} else if (!strcmp(name, "mpeg4")) {
		ao2_replace(ast_format_mp4, format);
	} else if (!strcmp(name, "vp8")) {
		ao2_replace(ast_format_vp8, format);
	} else if (!strcmp(name, "vp9")) {
		ao2_replace(ast_format_vp9, format);
	} else if (!strcmp(name, "red")) {
		ao2_replace(ast_format_t140_red, format);
	} else if (!strcmp(name, "t140")) {
		ao2_replace(ast_format_t140, format);
	} else if (!strcmp(name, "t38")) {
		ao2_replace(ast_format_t38, format);
	} else if (!strcmp(name, "none")) {
		ao2_replace(ast_format_none, format);
	} else if (!strcmp(name, "silk8")) {
		ao2_replace(ast_format_silk8, format);
	} else if (!strcmp(name, "silk12")) {
		ao2_replace(ast_format_silk12, format);
	} else if (!strcmp(name, "silk16")) {
		ao2_replace(ast_format_silk16, format);
	} else if (!strcmp(name, "silk24")) {
		ao2_replace(ast_format_silk24, format);
	}
}

int ast_format_cache_set(struct ast_format *format)
{
	SCOPED_AO2WRLOCK(lock, formats);
	struct ast_format *old_format;

	ast_assert(format != NULL);

	if (ast_strlen_zero(ast_format_get_name(format))) {
		return -1;
	}

	old_format = ao2_find(formats, ast_format_get_name(format), OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (old_format) {
		ao2_unlink_flags(formats, old_format, OBJ_NOLOCK);
	}
	ao2_link_flags(formats, format, OBJ_NOLOCK);

	set_cached_format(ast_format_get_name(format), format);

	ast_verb(2, "%s cached format with name '%s'\n",
		old_format ? "Updated" : "Created",
		ast_format_get_name(format));

	ao2_cleanup(old_format);

	return 0;
}

struct ast_format *__ast_format_cache_get(const char *name,
	const char *tag, const char *file, int line, const char *func)
{
	if (ast_strlen_zero(name)) {
		return NULL;
	}

	return __ao2_find(formats, name, OBJ_SEARCH_KEY, tag, file, line, func);
}

struct ast_format *ast_format_cache_get_slin_by_rate(unsigned int rate)
{
	if (rate >= 192000) {
		return ast_format_slin192;
	} else if (rate >= 96000) {
		return ast_format_slin96;
	} else if (rate >= 48000) {
		return ast_format_slin48;
	} else if (rate >= 44100) {
		return ast_format_slin44;
	} else if (rate >= 32000) {
		return ast_format_slin32;
	} else if (rate >= 24000) {
		return ast_format_slin24;
	} else if (rate >= 16000) {
		return ast_format_slin16;
	} else if (rate >= 12000) {
		return ast_format_slin12;
	}
	return ast_format_slin;
}

int ast_format_cache_is_slinear(struct ast_format *format)
{
	if ((ast_format_cmp(format, ast_format_slin) == AST_FORMAT_CMP_EQUAL)
		|| (ast_format_cmp(format, ast_format_slin12) == AST_FORMAT_CMP_EQUAL)
		|| (ast_format_cmp(format, ast_format_slin16) == AST_FORMAT_CMP_EQUAL)
		|| (ast_format_cmp(format, ast_format_slin24) == AST_FORMAT_CMP_EQUAL)
		|| (ast_format_cmp(format, ast_format_slin32) == AST_FORMAT_CMP_EQUAL)
		|| (ast_format_cmp(format, ast_format_slin44) == AST_FORMAT_CMP_EQUAL)
		|| (ast_format_cmp(format, ast_format_slin48) == AST_FORMAT_CMP_EQUAL)
		|| (ast_format_cmp(format, ast_format_slin96) == AST_FORMAT_CMP_EQUAL)
		|| (ast_format_cmp(format, ast_format_slin192) == AST_FORMAT_CMP_EQUAL)) {
		return 1;
	}

	return 0;
}
