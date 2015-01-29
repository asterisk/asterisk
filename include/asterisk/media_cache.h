/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief An in-memory media cache
 */

#ifndef _ASTERISK_MEDIA_CACHE_H
#define _ASTERISK_MEDIA_CACHE_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct ast_variable;

/*!
 * \brief Check if an item exists in the cache
 *
 * \param uri The unique URI for the media item
 *
 * \retval 0 uri does not exist in cache
 * \retval 1 uri does exist in cache
 */
int ast_media_cache_exists(const char *uri);

/*!
 * \brief Retrieve an item from the cache
 *
 * \param uri The unique URI for the media item
 * \param preferred_file_name The preferred name for the file storing the
 *                            media once it is retrieved. Can be NULL.
 * \param file_path Buffer to store the full path to the media in the
 *                  cache
 * \param len The length of the buffer pointed to by \c file_path
 *
 * \retval 0 The item was retrieved successfully
 * \retval -1 The item could not be retrieved
 *
 * Example Usage:
 * \code
 * char media[PATH_MAX];
 * int res;
 *
 * res = ast_media_cache_retrieve("http://localhost/foo.wav", NULL,
 * 			media, sizeof(media));
 * \endcode
 *
 * \details
 * Retrieving an item will cause the \ref bucket Bucket backend associated
 * with the URI scheme in \c uri to be queried. If the Bucket backend
 * does not require an update, the cached information is used to find the
 * file associated with \c uri, and \c file_path is populated with the
 * location of the media file associated with \c uri.
 *
 * If the item is not in the cache, the item will be retrieved using the
 * \ref bucket backend. When this occurs, if \c preferred_file_name is given,
 * it will be used as the destination file for the retrieval. When retrieval
 * of the media from the backend is complete, \c file_path is then populated
 * as before.
 */
int ast_media_cache_retrieve(const char *uri, const char *preferred_file_name,
	char *file_path, size_t len);

/*!
 * \brief Retrieve metadata from an item in the cache
 *
 * \param uri The unique URI for the media item
 * \param key The key of the metadata to retrieve
 * \param value Buffer to store the value in
 * \param len The length of the buffer pointed to by \c value
 *
 * \retval 0 The metadata was retrieved successfully
 * \retval -1 The metadata could not be retrieved
 *
 * Example Usage:
 * \code
 *
 * int res;
 * char file_size[32];
 *
 * res = ast_media_cache_retrieve_metadata("http://localhost/foo.wav", "size",
 * 			file_size, sizeof(file_size));
 * \endcode
 */
int ast_media_cache_retrieve_metadata(const char *uri, const char *key,
	char *value, size_t len);

/*!
 * \brief Create/update a cached media item
 *
 * \param uri The unique URI for the media item to store in the cache
 * \param file_path Full path to the media file to be cached
 * \param metadata Metadata to store with the cached item
 *
 * \retval 0 The item was cached
 * \retval -1 An error occurred when creating/updating the item
 *
 * Example Usage:
 * \code
 * int res;
 *
 * res = ast_media_cache_create_or_update("http://localhost/foo.wav",
 * 		"/tmp/foo.wav", NULL);
 * \endcode
 *
 * \note This method will overwrite whatever has been provided by the
 * \ref bucket backend.
 *
 * \details
 * While \ref ast_media_cache_retrieve is used to retrieve media from
 * some \ref bucket provider, this method allows for overwriting what
 * is provided by a backend with some local media. This is useful for
 * reconstructing or otherwise associating local media with a remote
 * URI, deferring updating of the media from the backend to some later
 * retrieval.
 */
int ast_media_cache_create_or_update(const char *uri, const char *file_path,
	struct ast_variable *metadata);

/*!
 * \brief Remove an item from the media cache
 *
 * \param uri The unique URI for the media item to store in the cache
 *
 * \retval 0 success
 * \retval -1 error
 *
 * Example Usage:
 * \code
 * int res;
 *
 * res = ast_media_cache_delete("http://localhost/foo.wav");
 * \endcode
 *
 * \details
 * This removes an item completely from the media cache. Any files local
 * on disk associated with the item are deleted as well.
 *
 * \note It is up to the \ref bucket implementation whether or not this
 * affects any non-local storage
 */
int ast_media_cache_delete(const char *uri);

/*!
 * \brief Initialize the media cache
 *
 * \note This should only be called once, during Asterisk initialization
 *
 * \retval 0 success
 * \retval -1 error
 */
int ast_media_cache_init(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_MEDIA_CACHE_H */
