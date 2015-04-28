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
 * \brief Format Capabilities API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _AST_FORMAT_CAP_H_
#define _AST_FORMAT_CAP_H_

#include "asterisk/codec.h"

/*! Capabilities are represented by an opaque structure statically defined in format_cap.c */
struct ast_format_cap;

enum ast_format_cap_flags {
	/*!
	 * Default format capabilities settings
	 */
	AST_FORMAT_CAP_FLAG_DEFAULT = 0,
};

/*!
 * \brief Allocate a new ast_format_cap structure
 *
 * \param flags Modifiers of struct behavior.
 *
 * \retval ast_format_cap object on success.
 * \retval NULL on failure.
 */
struct ast_format_cap *__ast_format_cap_alloc(enum ast_format_cap_flags flags,
	const char *tag, const char *file, int line, const char *func);

#define ast_format_cap_alloc(flags) \
	__ast_format_cap_alloc((flags), "ast_format_cap_alloc", \
		__FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ast_t_format_cap_alloc(flags, tag) \
	__ast_format_cap_alloc((flags), (tag), __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief Set the global framing.
 *
 * \param cap The capabilities structure.
 * \param framing The framing value (in milliseconds).
 *
 * \note This is used if a format does not provide a framing itself. Note that
 *       adding subsequent formats to the \c ast_format_cap structure may
 *       override this value, if the framing they require is less than the
 *       value set by this function.
 */
void ast_format_cap_set_framing(struct ast_format_cap *cap, unsigned int framing);

/*!
 * \brief Get the global framing.
 *
 * \param cap The capabilities structure.
 *
 * \retval 0 if no formats are in the structure and no framing has been provided
 * \retval The global framing value (in milliseconds)
 *
 * \note This will be the minimum framing allowed across all formats in the
 *       capabilities structure, or an overridden value
 */
unsigned int ast_format_cap_get_framing(const struct ast_format_cap *cap);

/*!
 * \brief Add format capability to capabilities structure.
 *
 * \param cap The capabilities structure to add to.
 * \param format The format to add.
 * \param framing The framing for the format (in milliseconds).
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note A reference to the format is taken and used in the capabilities structure.
 *
 * \note The order in which add is called determines the format preference order.
 *
 * \note If framing is specified here it overrides any global framing that has been set.
 */
int __ast_format_cap_append(struct ast_format_cap *cap, struct ast_format *format, unsigned int framing,
	const char *tag, const char *file, int line, const char *func);

#define ast_format_cap_append(cap, format, framing) \
	__ast_format_cap_append((cap), (format), (framing), "ast_format_cap_append", \
		__FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ast_t_format_cap_append(cap, format, framing, tag) \
	__ast_format_cap_append((cap), (format), (framing), (tag), \
		__FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief Add all codecs Asterisk knows about for a specific type to
 * the capabilities structure.
 *
 * \param cap The capabilities structure to add to.
 * \param type The type of formats to add.
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note A generic format with no attributes is created using the codec.
 *
 * \note If AST_MEDIA_TYPE_UNKNOWN is passed as the type all known codecs will be added.
 */
int ast_format_cap_append_by_type(struct ast_format_cap *cap, enum ast_media_type type);

/*!
 * \brief Append the formats of provided type in src to dst
 *
 * \param dst The destination capabilities structure
 * \param src The source capabilities structure
 * \param type The type of formats to append.
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note If AST_MEDIA_TYPE_UNKNOWN is passed as the type all known codecs will be added.
 */
int ast_format_cap_append_from_cap(struct ast_format_cap *dst, const struct ast_format_cap *src, enum ast_media_type type);

/*!
 * \brief Replace the formats of provided type in dst with equivalent formats from src
 *
 * \param dst The destination capabilities structure
 * \param src The source capabilities structure
 * \param type The type of formats to replace.
 *
 * \note If AST_MEDIA_TYPE_UNKNOWN is passed as the type all known codecs will be replaced.
 * \note Formats present in src but not dst will not be appended to dst.
 */
void ast_format_cap_replace_from_cap(struct ast_format_cap *dst, const struct ast_format_cap *src, enum ast_media_type type);

/*!
 * \brief Parse an "allow" or "deny" list and modify a format capabilities structure accordingly
 *
 * \param cap The capabilities structure to modify
 * \param list The list containing formats to append or remove
 * \param allowing If zero, start removing formats specified in the list. If non-zero,
 *        start appending formats specified in the list.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_format_cap_update_by_allow_disallow(struct ast_format_cap *cap, const char *list, int allowing);

/*!
 * \brief Get the number of formats present within the capabilities structure
 *
 * \param cap The capabilities structure
 *
 * \return the number of formats
 */
size_t ast_format_cap_count(const struct ast_format_cap *cap);

/*!
 * \brief Get the format at a specific index
 *
 * \param cap The capabilities structure
 * \param position The position to get
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note This is a zero based index.
 *
 * \note Formats are returned in order of preference.
 *
 * \note The reference count of the returned format is increased. It must be released using ao2_ref
 * or ao2_cleanup.
 */
struct ast_format *ast_format_cap_get_format(const struct ast_format_cap *cap, int position);

/*!
 * \brief Get the most preferred format for a particular media type
 *
 * \param cap The capabilities structure
 * \param type The type of media to get
 *
 * \retval non-NULL the preferred format
 * \retval NULL no media of \c type present
 *
 * \note The reference count of the returned format is increased. It must be released using ao2_ref
 * or ao2_cleanup.
 */
struct ast_format *ast_format_cap_get_best_by_type(const struct ast_format_cap *cap, enum ast_media_type type);

/*!
 * \brief Get the framing for a format
 *
 * \param cap The capabilities structure
 * \param format The format to retrieve
 *
 * \return the framing (in milliseconds)
 */
unsigned int ast_format_cap_get_format_framing(const struct ast_format_cap *cap, const struct ast_format *format);

/*!
 * \brief Remove format capability from capability structure.
 *
 * \note format must be an exact pointer match to remove from capabilities structure.
 *
 * \retval 0, remove was successful
 * \retval -1, remove failed. Could not find format to remove
 */
int ast_format_cap_remove(struct ast_format_cap *cap, struct ast_format *format);

/*!
 * \brief Remove all formats matching a specific format type.
 *
 * \param cap The capabilities structure
 * \param type The media type to remove formats of
 *
 * \note All formats can be removed by using the AST_MEDIA_TYPE_UNKNOWN type.
 */
void ast_format_cap_remove_by_type(struct ast_format_cap *cap, enum ast_media_type type);

/*!
 * \brief Find if input ast_format is within the capabilities of the ast_format_cap object
 * then return the compatible format from the capabilities structure in the result.
 *
 * \retval non-NULL if format is compatible
 * \retval NULL if not compatible
 *
 * \note The reference count of the returned format is increased. It must be released using ao2_ref
 * or ao2_cleanup.
 */
struct ast_format *ast_format_cap_get_compatible_format(const struct ast_format_cap *cap, const struct ast_format *format);

/*!
 * \brief Find if ast_format is within the capabilities of the ast_format_cap object.
 *
* \retval ast_format_cmp_res representing the result of the compatibility check between cap and format.
 */
enum ast_format_cmp_res ast_format_cap_iscompatible_format(const struct ast_format_cap *cap, const struct ast_format *format);

/*!
 * \brief Find the compatible formats between two capabilities structures
 *
 * \param cap1 The first capabilities structure
 * \param cap2 The second capabilities structure
 * \param[out] result The capabilities structure to place the results into
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note The preference order of cap1 is respected.
 *
 * \note If failure occurs the result format capabilities structure may contain a partial result.
 */
int ast_format_cap_get_compatible(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2,
	struct ast_format_cap *result);

/*!
 * \brief Determine if any joint capabilities exist between two capabilities structures
 *
 * \param cap1 The first capabilities structure
 * \param cap2 The second capabilities structure
 *
 * \retval 0 no joint capabilities exist
 * \retval 1 joint capabilities exist
 */
int ast_format_cap_iscompatible(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2);

/*!
 * \brief Determine if two capabilities structures are identical
 *
 * \param cap1 The first capabilities structure
 * \param cap2 The second capabilities structure
 *
 * \retval 0 capabilities are not identical
 * \retval 1 capabilities are identical
 */
int ast_format_cap_identical(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2);

/*!
 * \brief Find out if the capabilities structure has any formats
 * of a specific type.
 *
 * \retval 1 true
 * \retval 0 false, no formats of specific type.
 */
int ast_format_cap_has_type(const struct ast_format_cap *cap, enum ast_media_type type);

/*!
 * \brief Get the names of codecs of a set of formats
 *
 * \param cap The capabilities structure containing the formats
 * \param buf A \c ast_str buffer to populate with the names of the formats
 *
 * \return The contents of the buffer in \c buf
 */
const char *ast_format_cap_get_names(struct ast_format_cap *cap, struct ast_str **buf);

/*!
 * \brief Determine if a format cap has no formats in it.
 *
 * \param cap The format cap to check for emptiness
 * \retval 1 The format cap has zero formats or only ast_format_none
 * \retval 0 The format cap has at least one format
 */
int ast_format_cap_empty(struct ast_format_cap *cap);

#endif /* _AST_FORMAT_CAP_H */
