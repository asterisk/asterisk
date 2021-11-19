/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2019, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@digium.com>
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

#ifndef _AST_SERIALIZER_H
#define _AST_SERIALIZER_H

struct ast_threadpool;

/*!
 * Maintains a named pool of thread pooled taskprocessors. Also if configured
 * a shutdown group can be enabled that will ensure all serializers have
 * completed any assigned task before destruction.
 */
struct ast_serializer_pool;

/*!
 * \brief Destroy the serializer pool.
 *
 * Attempt to destroy the serializer pool. If a shutdown group has been enabled,
 * and times out waiting for threads to complete, then this function will return
 * the number of remaining threads, and the pool will not be destroyed.
 *
 * \param pool The pool to destroy
 */
int ast_serializer_pool_destroy(struct ast_serializer_pool *pool);

/*!
 * \brief Create a serializer pool.
 *
 * Create a serializer pool with an optional shutdown group. If a timeout greater
 * than -1 is specified then a shutdown group is enabled on the pool.
 *
 * \param name The base name for the pool, and used when building taskprocessor(s)
 * \param size The size of the pool
 * \param threadpool The backing threadpool to use
 * \param timeout The timeout used if using a shutdown group (-1 = disabled)
 *
 * \return A newly allocated serializer pool object
 * \retval NULL on error
 */
struct ast_serializer_pool *ast_serializer_pool_create(const char *name,
	unsigned int size, struct ast_threadpool *threadpool, int timeout);

/*!
 * \brief Retrieve the base name of the serializer pool.
 *
 * \param pool The pool object
 *
 * \return The base name given to the pool
 */
const char *ast_serializer_pool_name(const struct ast_serializer_pool *pool);

/*!
 * \brief Retrieve a serializer from the pool.
 *
 * \param pool The pool object
 *
 * \return A serializer/taskprocessor
 */
struct ast_taskprocessor *ast_serializer_pool_get(struct ast_serializer_pool *pool);

/*!
 * \brief Set taskprocessor alert levels for the serializers in the pool.
 *
 * \param pool The pool to destroy
 * \param high, low
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_serializer_pool_set_alerts(struct ast_serializer_pool *pool, long high, long low);

#endif /* _AST_SERIALIZER_H */
