/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
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
 * \brief Media Stream API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _AST_STREAM_H_
#define _AST_STREAM_H_

#include "asterisk/codec.h"

/*!
 * \brief Forward declaration for a stream, as it is opaque
 */
struct ast_stream;

/*!
 * \brief Forward declaration for a format capability
 */
struct ast_format_cap;

/*!
 * \brief States that a stream may be in
 */
enum ast_stream_state {
    /*!
     * \brief Set when the stream has been removed
     */
    AST_STREAM_STATE_REMOVED = 0,
    /*!
     * \brief Set when the stream is sending and receiving media
     */
    AST_STREAM_STATE_SENDRECV,
    /*!
     * \brief Set when the stream is sending media only
     */
    AST_STREAM_STATE_SENDONLY,
    /*!
     * \brief Set when the stream is receiving media only
     */
    AST_STREAM_STATE_RECVONLY,
    /*!
     * \brief Set when the stream is not sending OR receiving media
     */
    AST_STREAM_STATE_INACTIVE,
};

/*!
 * \brief Create a new media stream representation
 *
 * \param name A name for the stream
 * \param type The media type the stream is handling
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note This is NOT an AO2 object and has no locking. It is expected that a higher level object provides protection.
 *
 * \note The stream will default to an inactive state until changed.
 *
 * \since 15
 */
struct ast_stream *ast_stream_create(const char *name, enum ast_media_type type);

/*!
 * \brief Destroy a media stream representation
 *
 * \param stream The media stream
 *
 * \since 15
 */
void ast_stream_destroy(struct ast_stream *stream);

/*!
 * \brief Get the name of a stream
 *
 * \param stream The media stream
 *
 * \return The name of the stream
 *
 * \since 15
 */
const char *ast_stream_get_name(const struct ast_stream *stream);

/*!
 * \brief Get the media type of a stream
 *
 * \param stream The media stream
 *
 * \return The media type of the stream
 *
 * \since 15
 */
enum ast_media_type ast_stream_get_type(const struct ast_stream *stream);

/*!
 * \brief Change the media type of a stream
 *
 * \param stream The media stream
 * \param type The new media type
 *
 * \since 15
 */
void ast_stream_set_type(struct ast_stream *stream, enum ast_media_type type);

/*!
 * \brief Get the current negotiated formats of a stream
 *
 * \param stream The media stream
 *
 * \return The negotiated media formats
 *
 * \note The reference count is not increased
 *
 * \since 15
 */
struct ast_format_cap *ast_stream_get_formats(const struct ast_stream *stream);

/*!
 * \brief Set the current negotiated formats of a stream
 *
 * \param stream The media stream
 * \param caps The current negotiated formats
 *
 * \since 15
 */
void ast_stream_set_formats(struct ast_stream *stream, struct ast_format_cap *caps);

/*!
 * \brief Get the current state of a stream
 *
 * \param stream The media stream
 *
 * \return The state of the stream
 *
 * \since 15
 */
enum ast_stream_state ast_stream_get_state(const struct ast_stream *stream);

/*!
 * \brief Set the state of a stream
 *
 * \param stream The media stream
 * \param state The new state that the stream is in
 *
 * \note Used by stream creator to update internal state
 *
 * \since 15
 */
void ast_stream_set_state(struct ast_stream *stream, enum ast_stream_state state);

/*!
 * \brief Get the number of the stream
 *
 * \param stream The media stream
 *
 * \return The number of the stream
 *
 * \since 15
 */
unsigned int ast_stream_get_num(const struct ast_stream *stream);

#endif /* _AST_STREAM_H */
