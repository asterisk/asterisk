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
 * \brief Format Capabilities API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/logger.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/format_cache.h"
#include "asterisk/codec.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/vector.h"
#include "asterisk/linkedlists.h"
#include "asterisk/utils.h"

/*! \brief Structure used for capability formats, adds framing */
struct format_cap_framed {
	/*! \brief A pointer to the format */
	struct ast_format *format;
	/*! \brief The format framing size */
	unsigned int framing;
	/*! \brief Linked list information */
	AST_LIST_ENTRY(format_cap_framed) entry;
};

/*! \brief Format capabilities structure, holds formats + preference order + etc */
struct ast_format_cap {
	/*! \brief Vector of formats, indexed using the codec identifier */
	AST_VECTOR(, struct format_cap_framed_list) formats;
	/*! \brief Vector of formats, added in preference order */
	AST_VECTOR(, struct format_cap_framed *) preference_order;
	/*! \brief Global framing size, applies to all formats if no framing present on format */
	unsigned int framing;
};

/*! \brief Linked list for formats */
AST_LIST_HEAD_NOLOCK(format_cap_framed_list, format_cap_framed);

/*! \brief Dummy empty list for when we are inserting a new list */
static const struct format_cap_framed_list format_cap_framed_list_empty = AST_LIST_HEAD_NOLOCK_INIT_VALUE;

/*! \brief Destructor for format capabilities structure */
static void format_cap_destroy(void *obj)
{
	struct ast_format_cap *cap = obj;
	int idx;

	for (idx = 0; idx < AST_VECTOR_SIZE(&cap->formats); idx++) {
		struct format_cap_framed_list *list = AST_VECTOR_GET_ADDR(&cap->formats, idx);
		struct format_cap_framed *framed;

		while ((framed = AST_LIST_REMOVE_HEAD(list, entry))) {
			ao2_ref(framed, -1);
		}
	}
	AST_VECTOR_FREE(&cap->formats);

	for (idx = 0; idx < AST_VECTOR_SIZE(&cap->preference_order); idx++) {
		struct format_cap_framed *framed = AST_VECTOR_GET(&cap->preference_order, idx);

		/* This will always be non-null, unlike formats */
		ao2_ref(framed, -1);
	}
	AST_VECTOR_FREE(&cap->preference_order);
}

static inline void format_cap_init(struct ast_format_cap *cap, enum ast_format_cap_flags flags)
{
	AST_VECTOR_INIT(&cap->formats, 0);

	/* TODO: Look at common usage of this and determine a good starting point */
	AST_VECTOR_INIT(&cap->preference_order, 5);

	cap->framing = UINT_MAX;
}

struct ast_format_cap *__ast_format_cap_alloc(enum ast_format_cap_flags flags)
{
	struct ast_format_cap *cap;

	cap = ao2_alloc_options(sizeof(*cap), format_cap_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!cap) {
		return NULL;
	}

	format_cap_init(cap, flags);

	return cap;
}

struct ast_format_cap *__ast_format_cap_alloc_debug(enum ast_format_cap_flags flags, const char *tag, const char *file, int line, const char *func)
{
	struct ast_format_cap *cap;

	cap = __ao2_alloc_debug(sizeof(*cap), format_cap_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK, S_OR(tag, "ast_format_cap_alloc"), file, line, func, 1);
	if (!cap) {
		return NULL;
	}

	format_cap_init(cap, flags);

	return cap;
}

void ast_format_cap_set_framing(struct ast_format_cap *cap, unsigned int framing)
{
	cap->framing = framing;
}

/*! \brief Destructor for format capabilities framed structure */
static void format_cap_framed_destroy(void *obj)
{
	struct format_cap_framed *framed = obj;

	ao2_cleanup(framed->format);
}

static inline int format_cap_framed_init(struct format_cap_framed *framed, struct ast_format_cap *cap, struct ast_format *format, unsigned int framing)
{
	struct format_cap_framed_list *list;

	framed->framing = framing;

	if (ast_format_get_codec_id(format) >= AST_VECTOR_SIZE(&cap->formats)) {
		if (AST_VECTOR_INSERT(&cap->formats, ast_format_get_codec_id(format), format_cap_framed_list_empty)) {
			ao2_ref(framed, -1);
			return -1;
		}
	}
	list = AST_VECTOR_GET_ADDR(&cap->formats, ast_format_get_codec_id(format));

	/* Order doesn't matter for formats, so insert at the head for performance reasons */
	ao2_ref(framed, +1);
	AST_LIST_INSERT_HEAD(list, framed, entry);

	/* This takes the allocation reference */
	AST_VECTOR_APPEND(&cap->preference_order, framed);

	cap->framing = MIN(cap->framing, framing ? framing : ast_format_get_default_ms(format));

	return 0;
}

/*! \internal \brief Determine if \c format is in \c cap */
static int format_in_format_cap(struct ast_format_cap *cap, struct ast_format *format)
{
	struct format_cap_framed *framed;
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&cap->preference_order); i++) {
		framed = AST_VECTOR_GET(&cap->preference_order, i);

		if (ast_format_get_codec_id(format) == ast_format_get_codec_id(framed->format)) {
			return 1;
		}
	}

	return 0;
}

int __ast_format_cap_append(struct ast_format_cap *cap, struct ast_format *format, unsigned int framing)
{
	struct format_cap_framed *framed;

	ast_assert(format != NULL);

	if (format_in_format_cap(cap, format)) {
		return 0;
	}

	framed = ao2_alloc_options(sizeof(*framed), format_cap_framed_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!framed) {
		return -1;
	}
	framed->format = ao2_bump(format);

	return format_cap_framed_init(framed, cap, format, framing);
}

int __ast_format_cap_append_debug(struct ast_format_cap *cap, struct ast_format *format, unsigned int framing, const char *tag, const char *file, int line, const char *func)
{
	struct format_cap_framed *framed;

	ast_assert(format != NULL);

	if (format_in_format_cap(cap, format)) {
		return 0;
	}

	framed = ao2_alloc_options(sizeof(*framed), format_cap_framed_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!framed) {
		return -1;
	}

	__ao2_ref_debug(format, +1, S_OR(tag, "ast_format_cap_append"), file, line, func);
	framed->format = format;

	return format_cap_framed_init(framed, cap, format, framing);
}

int ast_format_cap_append_by_type(struct ast_format_cap *cap, enum ast_media_type type)
{
	int id;

	for (id = 1; id < ast_codec_get_max(); ++id) {
		struct ast_codec *codec = ast_codec_get_by_id(id);
		struct ast_format *format;
		int res;

		if (!codec) {
			continue;
		}

		if ((type != AST_MEDIA_TYPE_UNKNOWN) && codec->type != type) {
			ao2_ref(codec, -1);
			continue;
		}

		format = ast_format_create(codec);
		ao2_ref(codec, -1);

		if (!format) {
			return -1;
		}

		/* Use the global framing or default framing of the codec */
		res = ast_format_cap_append(cap, format, 0);
		ao2_ref(format, -1);

		if (res) {
			return -1;
		}
	}

	return 0;
}

int ast_format_cap_append_from_cap(struct ast_format_cap *dst, const struct ast_format_cap *src,
	enum ast_media_type type)
{
	int idx, res = 0;

	for (idx = 0; (idx < AST_VECTOR_SIZE(&src->preference_order)) && !res; ++idx) {
		struct format_cap_framed *framed = AST_VECTOR_GET(&src->preference_order, idx);

		if (type == AST_MEDIA_TYPE_UNKNOWN || ast_format_get_type(framed->format) == type) {
			res = ast_format_cap_append(dst, framed->format, framed->framing);
		}
	}

	return res;
}

static int format_cap_replace(struct ast_format_cap *cap, struct ast_format *format, unsigned int framing)
{
	struct format_cap_framed *framed;
	int i;

	ast_assert(format != NULL);

	for (i = 0; i < AST_VECTOR_SIZE(&cap->preference_order); i++) {
		framed = AST_VECTOR_GET(&cap->preference_order, i);

		if (ast_format_get_codec_id(format) == ast_format_get_codec_id(framed->format)) {
			ao2_t_replace(framed->format, format, "replacing with new format");
			framed->framing = framing;
			return 0;
		}
	}

	return -1;
}

void ast_format_cap_replace_from_cap(struct ast_format_cap *dst, const struct ast_format_cap *src,
	enum ast_media_type type)
{
	int idx;

	for (idx = 0; (idx < AST_VECTOR_SIZE(&src->preference_order)); ++idx) {
		struct format_cap_framed *framed = AST_VECTOR_GET(&src->preference_order, idx);

		if (type == AST_MEDIA_TYPE_UNKNOWN || ast_format_get_type(framed->format) == type) {
			format_cap_replace(dst, framed->format, framed->framing);
		}
	}
}

int ast_format_cap_update_by_allow_disallow(struct ast_format_cap *cap, const char *list, int allowing)
{
	int res = 0, all = 0, iter_allowing;
	char *parse = NULL, *this = NULL, *psize = NULL;

	if (!allowing && ast_strlen_zero(list)) {
		return 0;
	}

	parse = ast_strdupa(list);
	while ((this = strsep(&parse, ","))) {
		int framems = 0;
		struct ast_format *format = NULL;

		iter_allowing = allowing;
		if (*this == '!') {
			this++;
			iter_allowing = !allowing;
		}
		if ((psize = strrchr(this, ':'))) {
			*psize++ = '\0';
			ast_debug(1, "Packetization for codec: %s is %s\n", this, psize);
			if (!sscanf(psize, "%30d", &framems) || (framems < 0)) {
				framems = 0;
				res = -1;
				ast_log(LOG_WARNING, "Bad packetization value for codec %s\n", this);
				continue;
			}
		}
		all = strcasecmp(this, "all") ? 0 : 1;

		if (!all && !(format = ast_format_cache_get(this))) {
			ast_log(LOG_WARNING, "Cannot %s unknown format '%s'\n", iter_allowing ? "allow" : "disallow", this);
			res = -1;
			continue;
		}

		if (cap) {
			if (iter_allowing) {
				if (all) {
					ast_format_cap_append_by_type(cap, AST_MEDIA_TYPE_UNKNOWN);
				} else {
					ast_format_cap_append(cap, format, framems);
				}
			} else {
				if (all) {
					ast_format_cap_remove_by_type(cap, AST_MEDIA_TYPE_UNKNOWN);
				} else {
					ast_format_cap_remove(cap, format);
				}
			}
		}

		ao2_cleanup(format);
	}
	return res;
}

size_t ast_format_cap_count(const struct ast_format_cap *cap)
{
	return AST_VECTOR_SIZE(&cap->preference_order);
}

struct ast_format *ast_format_cap_get_format(const struct ast_format_cap *cap, int position)
{
	struct format_cap_framed *framed;

	ast_assert(position < AST_VECTOR_SIZE(&cap->preference_order));

	if (position >= AST_VECTOR_SIZE(&cap->preference_order)) {
		return NULL;
	}

	framed = AST_VECTOR_GET(&cap->preference_order, position);

	ast_assert(framed->format != ast_format_none);
	ao2_ref(framed->format, +1);
	return framed->format;
}

struct ast_format *ast_format_cap_get_best_by_type(const struct ast_format_cap *cap, enum ast_media_type type)
{
	int i;

	if (type == AST_MEDIA_TYPE_UNKNOWN) {
		return ast_format_cap_get_format(cap, 0);
	}

	for (i = 0; i < AST_VECTOR_SIZE(&cap->preference_order); i++) {
		struct format_cap_framed *framed = AST_VECTOR_GET(&cap->preference_order, i);

		if (ast_format_get_type(framed->format) == type) {
			ao2_ref(framed->format, +1);
			ast_assert(framed->format != ast_format_none);
			return framed->format;
		}
	}

	return NULL;
}

unsigned int ast_format_cap_get_framing(const struct ast_format_cap *cap)
{
	return (cap->framing != UINT_MAX) ? cap->framing : 0;
}

unsigned int ast_format_cap_get_format_framing(const struct ast_format_cap *cap, const struct ast_format *format)
{
	unsigned int framing;
	struct format_cap_framed_list *list;
	struct format_cap_framed *framed, *result = NULL;

	if (ast_format_get_codec_id(format) >= AST_VECTOR_SIZE(&cap->formats)) {
		return 0;
	}

	framing = cap->framing != UINT_MAX ? cap->framing : ast_format_get_default_ms(format);
	list = AST_VECTOR_GET_ADDR(&cap->formats, ast_format_get_codec_id(format));

	AST_LIST_TRAVERSE(list, framed, entry) {
		enum ast_format_cmp_res res = ast_format_cmp(format, framed->format);

		if (res == AST_FORMAT_CMP_NOT_EQUAL) {
			continue;
		}

		result = framed;

		if (res == AST_FORMAT_CMP_EQUAL) {
			break;
		}
	}

	if (result && result->framing) {
		framing = result->framing;
	}

	return framing;
}

/*!
 * \brief format_cap_framed comparator for AST_VECTOR_REMOVE_CMP_ORDERED()
 *
 * \param elem Element to compare against
 * \param value Value to compare with the vector element.
 *
 * \return 0 if element does not match.
 * \return Non-zero if element matches.
 */
#define FORMAT_CAP_FRAMED_ELEM_CMP(elem, value) ((elem)->format == (value))

/*!
 * \brief format_cap_framed vector element cleanup.
 *
 * \param elem Element to cleanup
 *
 * \return Nothing
 */
#define FORMAT_CAP_FRAMED_ELEM_CLEANUP(elem)  ao2_cleanup((elem))

int ast_format_cap_remove(struct ast_format_cap *cap, struct ast_format *format)
{
	struct format_cap_framed_list *list;
	struct format_cap_framed *framed;

	ast_assert(format != NULL);

	if (ast_format_get_codec_id(format) >= AST_VECTOR_SIZE(&cap->formats)) {
		return -1;
	}

	list = AST_VECTOR_GET_ADDR(&cap->formats, ast_format_get_codec_id(format));

	AST_LIST_TRAVERSE_SAFE_BEGIN(list, framed, entry) {
		if (!FORMAT_CAP_FRAMED_ELEM_CMP(framed, format)) {
			continue;
		}

		AST_LIST_REMOVE_CURRENT(entry);
		FORMAT_CAP_FRAMED_ELEM_CLEANUP(framed);
		break;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	return AST_VECTOR_REMOVE_CMP_ORDERED(&cap->preference_order, format,
		FORMAT_CAP_FRAMED_ELEM_CMP, FORMAT_CAP_FRAMED_ELEM_CLEANUP);
}

void ast_format_cap_remove_by_type(struct ast_format_cap *cap, enum ast_media_type type)
{
	int idx;

	for (idx = 0; idx < AST_VECTOR_SIZE(&cap->formats); ++idx) {
		struct format_cap_framed_list *list = AST_VECTOR_GET_ADDR(&cap->formats, idx);
		struct format_cap_framed *framed;

		AST_LIST_TRAVERSE_SAFE_BEGIN(list, framed, entry) {
			if ((type != AST_MEDIA_TYPE_UNKNOWN) &&
				ast_format_get_type(framed->format) != type) {
				continue;
			}

			AST_LIST_REMOVE_CURRENT(entry);
			AST_VECTOR_REMOVE_CMP_ORDERED(&cap->preference_order, framed->format,
				FORMAT_CAP_FRAMED_ELEM_CMP, FORMAT_CAP_FRAMED_ELEM_CLEANUP);
			ao2_ref(framed, -1);
		}
		AST_LIST_TRAVERSE_SAFE_END;
	}
}

struct ast_format *ast_format_cap_get_compatible_format(const struct ast_format_cap *cap, const struct ast_format *format)
{
	struct format_cap_framed_list *list;
	struct format_cap_framed *framed;
	struct ast_format *result = NULL;

	ast_assert(format != NULL);

	if (ast_format_get_codec_id(format) >= AST_VECTOR_SIZE(&cap->formats)) {
		return NULL;
	}

	list = AST_VECTOR_GET_ADDR(&cap->formats, ast_format_get_codec_id(format));

	AST_LIST_TRAVERSE(list, framed, entry) {
		enum ast_format_cmp_res res = ast_format_cmp(format, framed->format);

		if (res == AST_FORMAT_CMP_NOT_EQUAL) {
			continue;
		}

		/* Replace any current result, this one will also be a subset OR an exact match */
		ao2_cleanup(result);

		result = ast_format_joint(format, framed->format);

		/* If it's a match we can do no better so return asap */
		if (res == AST_FORMAT_CMP_EQUAL) {
			break;
		}
	}

	return result;
}

enum ast_format_cmp_res ast_format_cap_iscompatible_format(const struct ast_format_cap *cap,
	const struct ast_format *format)
{
	enum ast_format_cmp_res res = AST_FORMAT_CMP_NOT_EQUAL;
	struct format_cap_framed_list *list;
	struct format_cap_framed *framed;

	ast_assert(format != NULL);

	if (ast_format_get_codec_id(format) >= AST_VECTOR_SIZE(&cap->formats)) {
		return AST_FORMAT_CMP_NOT_EQUAL;
	}

	list = AST_VECTOR_GET_ADDR(&cap->formats, ast_format_get_codec_id(format));

	AST_LIST_TRAVERSE(list, framed, entry) {
		enum ast_format_cmp_res cmp = ast_format_cmp(format, framed->format);

		if (cmp == AST_FORMAT_CMP_NOT_EQUAL) {
			continue;
		}

		res = cmp;

		if (res == AST_FORMAT_CMP_EQUAL) {
			break;
		}
	}

	return res;
}

int ast_format_cap_has_type(const struct ast_format_cap *cap, enum ast_media_type type)
{
	int idx;

	for (idx = 0; idx < AST_VECTOR_SIZE(&cap->preference_order); ++idx) {
		struct format_cap_framed *framed = AST_VECTOR_GET(&cap->preference_order, idx);

		if (ast_format_get_type(framed->format) == type) {
			return 1;
		}
	}

	return 0;
}

int ast_format_cap_get_compatible(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2,
	struct ast_format_cap *result)
{
	int idx, res = 0;

	for (idx = 0; idx < AST_VECTOR_SIZE(&cap1->preference_order); ++idx) {
		struct format_cap_framed *framed = AST_VECTOR_GET(&cap1->preference_order, idx);
		struct ast_format *format;

		format = ast_format_cap_get_compatible_format(cap2, framed->format);
		if (!format) {
			continue;
		}

		res = ast_format_cap_append(result, format, framed->framing);
		ao2_ref(format, -1);

		if (res) {
			break;
		}
	}

	return res;
}

int ast_format_cap_iscompatible(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2)
{
	int idx;

	for (idx = 0; idx < AST_VECTOR_SIZE(&cap1->preference_order); ++idx) {
		struct format_cap_framed *framed = AST_VECTOR_GET(&cap1->preference_order, idx);

		if (ast_format_cap_iscompatible_format(cap2, framed->format) != AST_FORMAT_CMP_NOT_EQUAL) {
			return 1;
		}
	}

	return 0;
}

static int internal_format_cap_identical(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2)
{
	int idx;
	struct ast_format *tmp;

	for (idx = 0; idx < AST_VECTOR_SIZE(&cap1->preference_order); ++idx) {
		tmp = ast_format_cap_get_format(cap1, idx);

		if (ast_format_cap_iscompatible_format(cap2, tmp) != AST_FORMAT_CMP_EQUAL) {
			ao2_ref(tmp, -1);
			return 0;
		}

		ao2_ref(tmp, -1);
	}

	return 1;
}

int ast_format_cap_identical(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2)
{
	if (AST_VECTOR_SIZE(&cap1->preference_order) != AST_VECTOR_SIZE(&cap2->preference_order)) {
		return 0; /* if they are not the same size, they are not identical */
	}

	if (!internal_format_cap_identical(cap1, cap2)) {
		return 0;
	}

	return internal_format_cap_identical(cap2, cap1);
}

const char *ast_format_cap_get_names(struct ast_format_cap *cap, struct ast_str **buf)
{
	int i;

	ast_str_set(buf, 0, "(");

	if (!AST_VECTOR_SIZE(&cap->preference_order)) {
		ast_str_append(buf, 0, "nothing)");
		return ast_str_buffer(*buf);
	}

	for (i = 0; i < AST_VECTOR_SIZE(&cap->preference_order); ++i) {
		int res;
		struct format_cap_framed *framed = AST_VECTOR_GET(&cap->preference_order, i);

		res = ast_str_append(buf, 0, "%s%s", ast_format_get_name(framed->format),
			i < AST_VECTOR_SIZE(&cap->preference_order) - 1 ? "|" : "");
		if (res < 0) {
			break;
		}
	}
	ast_str_append(buf, 0, ")");

	return ast_str_buffer(*buf);
}

int ast_format_cap_empty(struct ast_format_cap *cap)
{
	int count = ast_format_cap_count(cap);

	if (count > 1) {
		return 0;
	}

	if (count == 0 || AST_VECTOR_GET(&cap->preference_order, 0)->format == ast_format_none) {
		return 1;
	}

	return 0;
}
