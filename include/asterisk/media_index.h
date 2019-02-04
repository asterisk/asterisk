/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Kinsey Moore <kmoore@digium.com>
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
 * \brief Media file format and description indexing engine.
 */

#ifndef _ASTERISK_MEDIA_INDEX_H
#define _ASTERISK_MEDIA_INDEX_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct ast_format_cap;

/*!
 * \brief Object representing a media index
 */
struct ast_media_index;

/*!
 * \brief Creates a new media index
 *
 * \param base_dir Base directory for indexing
 *
 * \retval NULL on error
 * \retval A new AO2 refcounted media index
 */
struct ast_media_index *ast_media_index_create(
	const char *base_dir);

/*!
 * \brief Get the description for a media file
 *
 * \param index Media index in which to query information
 * \param filename Name of the file for which to get the description
 * \param variant Media variant for which to get the description
 *
 * \retval NULL if not found
 * \return The description requested (must be copied to be kept)
 */
const char *ast_media_get_description(struct ast_media_index *index, const char *filename, const char *variant);

/*!
 * \brief Get the ast_format_cap for a media file
 *
 * \param index Media index in which to query information
 * \param filename Name of the file for which to get the description
 * \param variant Media variant for which to get the description
 *
 * \retval NULL if not found
 * \return a copy of the format capabilities (must be destroyed with ast_format_cap_destroy)
 */
struct ast_format_cap *ast_media_get_format_cap(struct ast_media_index *index, const char *filename, const char *variant);

/*!
 * \brief Get the languages in which a media file is available
 *
 * \param index Media index in which to query information
 * \param filename Name of the file for which to get available languages
 *
 * \retval NULL on error
 * \return an ast_str_container filled with language strings
 */
struct ao2_container *ast_media_get_variants(struct ast_media_index *index, const char *filename);

/*!
 * \brief Get the a container of all media available on the system
 *
 * \param index Media index in which to query information
 *
 * \retval NULL on error
 * \return an ast_str_container filled with media file name strings
 */
struct ao2_container *ast_media_get_media(struct ast_media_index *index);

/*!
 * \brief Update a media index
 *
 * \param index Media index in which to query information
 * \param variant Media variant for which to get the description
 *
 * \retval non-zero on error
 * \return zero on success
 */
int ast_media_index_update(struct ast_media_index *index,
	const char *variant);

/*!
 * \brief Update a media index for a specific sound file
 *
 * \since 13.25.0
 * \since 16.2.0
 *
 * \param index Media index in which to query information
 * \param variant Media variant for which to get the description
 * \param filename Sound file name without extension
 *
 * \note If filename is NULL, this function will act as
 * \ref ast_media_index_update and add all sound files to the index.
 *
 * \retval non-zero on error
 * \return zero on success
 */
int ast_media_index_update_for_file(struct ast_media_index *index,
	const char *variant, const char *filename);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_MEDIA_INDEX_H */
