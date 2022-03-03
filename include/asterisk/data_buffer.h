/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2018, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
 * Ben Ford <bford@digium.com>
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
 * \brief Data Buffer API
 *
 * A data buffer acts as a ring buffer of data. It is given a fixed
 * number of data packets to store (which may be dynamically changed).
 * Given a number it will store a data packet at that position relative
 * to the others. Given a number it will retrieve the given data packet
 * if it is present. This is purposely a storage of arbitrary things so
 * that it can be used for multiple things.
 *
 * \author Joshua Colp <jcolp@digium.com>
 * \author Ben Ford <bford@digium.com>
 */

#ifndef _AST_DATA_BUFFER_H_
#define _AST_DATA_BUFFER_H_

/*!
 * \brief A buffer of data payloads.
 */
struct ast_data_buffer;

/*!
 * \brief A callback function to free a data payload in a data buffer
 *
 * \param data The data payload
 */
typedef void (*ast_data_buffer_free_callback)(void *data);

/*!
 * \brief Allocate a data buffer
 *
 * \param free_fn Callback function to free a data payload
 * \param size The maximum number of data payloads to contain in the data buffer
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note free_fn can be NULL. It is up to the consumer of this API to ensure that memory is
 * managed appropriately.
 *
 * \since 15.4.0
 */
struct ast_data_buffer *ast_data_buffer_alloc(ast_data_buffer_free_callback free_fn, size_t size);

/*!
 * \brief Resize a data buffer
 *
 * \param buffer The data buffer
 * \param size The new maximum size of the data buffer
 *
 * \note If the data buffer is shrunk any old data payloads will be freed using the configured callback.
 * The data buffer is flexible and can be used for multiple purposes. Therefore it is up to the
 * caller of the function to know whether or not a buffer should have its size changed. Increasing
 * the size of the buffer may make sense in some scenarios, but shrinking should always be handled
 * with caution since data can be lost.
 *
 * \since 15.4.0
 */
void ast_data_buffer_resize(struct ast_data_buffer *buffer, size_t size);

/*!
 * \brief Place a data payload at a position in the data buffer
 *
 * \param buffer The data buffer
 * \param pos The position of the data payload
 * \param payload The data payload
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note It is up to the consumer of this API to ensure proper memory management of data payloads
 *
 * \since 15.4.0
 */
int ast_data_buffer_put(struct ast_data_buffer *buffer, size_t pos, void *payload);

/*!
 * \brief Retrieve a data payload from the data buffer
 *
 * \param buffer The data buffer
 * \param pos The position of the data payload
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note This does not remove the data payload from the data buffer. It will be removed when it is displaced.
 *
 * \since 15.4.0
 */
void *ast_data_buffer_get(const struct ast_data_buffer *buffer, size_t pos);

/*!
 * \brief Remove a data payload from the data buffer
 *
 * \param buffer The data buffer
 * \param pos The position of the data payload
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note This DOES remove the data payload from the data buffer. It does not free it, though.
 *
 * \since 15.5.0
 */
void *ast_data_buffer_remove(struct ast_data_buffer *buffer, size_t pos);

/*!
 * \brief Remove the first payload from the data buffer
 *
 * \param buffer The data buffer
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note This DOES remove the data payload from the data buffer.
 *
 * \since 15.5.0
 */
void *ast_data_buffer_remove_head(struct ast_data_buffer *buffer);

/*!
 * \brief Free a data buffer (and all held data payloads)
 *
 * \param buffer The data buffer
 *
 * \since 15.4.0
 */
void ast_data_buffer_free(struct ast_data_buffer *buffer);

/*!
 * \brief Return the number of payloads in a data buffer
 *
 * \param buffer The data buffer
 *
 * \return the number of data payloads
 *
 * \since 15.4.0
 */
size_t ast_data_buffer_count(const struct ast_data_buffer *buffer);

/*!
 * \brief Return the maximum number of payloads a data buffer can hold
 *
 * \param buffer The data buffer
 *
 * \return the maximum number of data payloads
 *
 * \since 15.4.0
 */
size_t ast_data_buffer_max(const struct ast_data_buffer *buffer);

#endif /* _AST_DATA_BUFFER_H */
