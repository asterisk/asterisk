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
 * \brief The topology of a set of streams
 */
struct ast_stream_topology;

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
struct ast_stream *ast_stream_alloc(const char *name, enum ast_media_type type);

/*!
 * \brief Destroy a media stream representation
 *
 * \param stream The media stream
 *
 * \since 15
 */
void ast_stream_free(struct ast_stream *stream);

/*!
 * \brief Create a deep clone of an existing stream
 *
 * \param stream The existing stream
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \since 15
 */
struct ast_stream *ast_stream_clone(const struct ast_stream *stream);

/*!
 * \brief Get the name of a stream
 *
 * \param stream The media stream
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \since 15
 */
const char *ast_stream_get_name(const struct ast_stream *stream);

/*!
 * \brief Get the media type of a stream
 *
 * \param stream The media stream
 *
 * \return The media type of the stream (AST_MEDIA_TYPE_UNKNOWN on error)
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
 * \retval non-NULL success
 * \retval NULL failure
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
 * \note The new format capabilities structure has its refcount bumped and
 * any existing format capabilities structure has its refcount decremented.
 *
 * \since 15
 */
void ast_stream_set_formats(struct ast_stream *stream, struct ast_format_cap *caps);

/*!
 * \brief Get the current state of a stream
 *
 * \param stream The media stream
 *
 * \return The state of the stream (AST_STREAM_STATE_UNKNOWN on error)
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
 * \brief Get the position of the stream in the topology
 *
 * \param stream The media stream
 *
 * \return The position of the stream (-1 on error)
 *
 * \since 15
 */
int ast_stream_get_position(const struct ast_stream *stream);

/*!
 * \brief Create a stream topology
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \since 15
 */
struct ast_stream_topology *ast_stream_topology_alloc(void);

/*!
 * \brief Create a deep clone of an existing stream topology
 *
 * \param topology The existing topology of streams
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \since 15
 */
struct ast_stream_topology *ast_stream_topology_clone(
	const struct ast_stream_topology *topology);

/*!
 * \brief Destroy a stream topology
 *
 * \param topology The topology of streams
 *
 * \note All streams contained within the topology will be destroyed
 *
 * \since 15
 */
void ast_stream_topology_free(struct ast_stream_topology *topology);

/*!
 * \brief Append a stream to the topology
 *
 * \param topology The topology of streams
 * \param stream The stream to append
 *
 * \returns the position of the stream in the topology (-1 on error)
 *
 * \since 15
 */
int ast_stream_topology_append_stream(struct ast_stream_topology *topology,
	struct ast_stream *stream);

/*!
 * \brief Get the number of streams in a topology
 *
 * \param topology The topology of streams
 *
 * \return the number of streams (-1 on error)
 *
 * \since 15
 */
int ast_stream_topology_get_count(const struct ast_stream_topology *topology);

/*!
 * \brief Get a specific stream from the topology
 *
 * \param topology The topology of streams
 * \param position The topology position to get
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \since 15
 */
struct ast_stream *ast_stream_topology_get_stream(
	const struct ast_stream_topology *topology, unsigned int position);

/*!
 * \brief Set a specific position in a topology
 *
 * \param topology The topology of streams
 * \param position The topology position to set
 * \param stream The stream to put in its place
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note If an existing stream exists it will be destroyed
 *
 * \note You can overwrite an existing position in the topology or set
 * the first unused position.  You can't set positions beyond that.
 *
 * \since 15
 */
int ast_stream_topology_set_stream(struct ast_stream_topology *topology,
	unsigned int position, struct ast_stream *stream);

/*!
 * \brief A helper function that, given a format capabilities structure,
 * creates a topology and separates the media types in format_cap into
 * separate streams.
 *
 * \param caps The format capabilities structure
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note The format capabilities reference is NOT altered by this function
 * since a new format capabilities structure is created for each media type.
 *
 * \note Each stream will have its name set to the corresponding media type.
 * For example: "AST_MEDIA_TYPE_AUDIO".
 *
 * \note Each stream will be set to the sendrecv state.
 *
 * \since 15
 */
struct ast_stream_topology *ast_stream_topology_create_from_format_cap(
	struct ast_format_cap *cap);

/*!
 * \brief Gets the first stream of a specific type from the topology
 *
 * \param topology The topology of streams
 * \param type The media type
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \since 15
 */
struct ast_stream *ast_stream_topology_get_first_stream_by_type(
	const struct ast_stream_topology *topology,
	enum ast_media_type type);

#endif /* _AST_STREAM_H */
