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
#include "asterisk/vector.h"

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
 * \brief A mapping of two topologies.
 */
struct ast_stream_topology_map;

typedef void (*ast_stream_data_free_fn)(void *);

/*!
 * \brief States that a stream may be in
 */
enum ast_stream_state {
	/*!
	 * \brief Set when the stream has been removed/declined
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
 * \param Optional name for cloned stream. If NULL, then existing stream's name is copied.
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note Opaque data pointers set with ast_stream_set_data() are not part
 * of the deep clone.  We have no way to clone the data.
 *
 * \since 15
 */
struct ast_stream *ast_stream_clone(const struct ast_stream *stream, const char *name);

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
 * \brief Convert the state of a stream into a string
 *
 * \param state The stream state
 *
 * \return The state of the stream in string format
 *
 * \since 15
 */
const char *ast_stream_state2str(enum ast_stream_state state);

/*!
 * \brief Convert a string to a stream state
 *
 * \param str The string to convert
 *
 * \return The stream state
 *
 * \since 15.0.0
 */
enum ast_stream_state ast_stream_str2state(const char *str);

/*!
 * \brief Get a stream metadata value
 *
 * \param stream The media stream
 * \param m_key  An arbitrary metadata key
 *
 * \retval non-NULL metadata value
 * \retval NULL failure or not found
 *
 * \since 15.5
 */
const char *ast_stream_get_metadata(const struct ast_stream *stream,
	const char *m_key);

/*!
 * \brief Get all stream metadata keys
 *
 * \param stream The media stream
 *
 * \retval An ast_variable list of the metadata key/value pairs.
 * \retval NULL if error or no variables are set.
 *
 * When you're finished with the list, you must call
 *  ast_variables_destroy(list);
 *
 * \since 15.5
 */
struct ast_variable *ast_stream_get_metadata_list(const struct ast_stream *stream);

/*!
 * \brief Set a stream metadata value
 *
 * \param stream The media stream
 * \param m_key  An arbitrary metadata key
 * \param value  String metadata value or NULL to remove existing value
 *
 * \retval -1 failure
 * \retval 0  success
 *
 * \since 15.5
 */
int ast_stream_set_metadata(struct ast_stream *stream, const char *m_key, const char *value);

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
 * \brief Get rtp_codecs associated with the stream
 *
 * \param stream The media stream
 *
 * \return The rtp_codecs
 *
 * \since 15.5
 */
struct ast_rtp_codecs *ast_stream_get_rtp_codecs(const struct ast_stream *stream);

/*!
 * \brief Set rtp_codecs associated with the stream
 *
 * \param stream The media stream
 * \param rtp_codecs The rtp_codecs
 *
 * \since 15.5
 */
void ast_stream_set_rtp_codecs(struct ast_stream *stream, struct ast_rtp_codecs *rtp_codecs);

/*!
 * \brief Create a stream topology
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \since 15
 *
 * \note This returns an ao2 refcounted object
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
 *
 * \note This returns an ao2 refcounted object
 */
struct ast_stream_topology *ast_stream_topology_clone(
	const struct ast_stream_topology *topology);

/*!
 * \brief Compare two stream topologies to see if they are equal
 *
 * \param left The left topology
 * \param right The right topology
 *
 * \retval 1 topologies are equivalent
 * \retval 0 topologies differ
 *
 * \since 15
 */
int ast_stream_topology_equal(const struct ast_stream_topology *left,
	const struct ast_stream_topology *right);

/*!
 * \brief Unreference and destroy a stream topology
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
 * \brief Delete a specified stream from the given topology.
 * \since 15.0.0
 *
 * \param topology The topology of streams.
 * \param position The topology position to delete.
 *
 * \note Deleting a stream will completely remove it from the topology
 * as if it never existed in it.  i.e., Any following stream positions
 * will shift down so there is no gap.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 *
 * \return Nothing
 */
int ast_stream_topology_del_stream(struct ast_stream_topology *topology,
	unsigned int position);

/*!
 * \brief A helper function that, given a format capabilities structure,
 * creates a topology and separates the media types in format_cap into
 * separate streams.
 *
 * \param caps The format capabilities structure (NULL creates an empty topology)
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note The format capabilities reference is NOT altered by this function
 * since a new format capabilities structure is created for each media type.
 *
 * \note Each stream will have its name set to the corresponding media type.
 * For example: "audio".
 *
 * \note Each stream will be set to the sendrecv state.
 *
 * \since 15
 */
struct ast_stream_topology *ast_stream_topology_create_from_format_cap(
	struct ast_format_cap *cap);

/*!
 * \brief Create a format capabilities structure representing the topology.
 *
 * \details
 * A helper function that, given a stream topology, creates a format
 * capabilities structure containing all formats from all active streams.
 *
 * \param topology The topology of streams
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note The stream topology is NOT altered by this function.
 *
 * \since 15
 */
struct ast_format_cap *ast_format_cap_from_stream_topology(
	struct ast_stream_topology *topology);

/*!
 * \brief Gets the first active stream of a specific type from the topology
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

/*!
 * \brief Map a given topology's streams to the given types.
 *
 * \note The given vectors in which mapping values are placed are reset by
 *       this function. This means if those vectors already contain mapping
 *       values they will be lost.
 *
 * \param topology The topology to map
 * \param types The media types to be mapped
 * \param v0 Index mapping of topology to types
 * \param v1 Index mapping of types to topology
 *
 * \since 15
 */
void ast_stream_topology_map(const struct ast_stream_topology *topology,
	struct ast_vector_int *types, struct ast_vector_int *v0, struct ast_vector_int *v1);

/*!
 * \brief Get the stream group that a stream is part of
 *
 * \param stream The stream
 *
 * \return the numerical stream group (-1 if not in a group)
 *
 * \since 15.2.0
 */
int ast_stream_get_group(const struct ast_stream *stream);

/*!
 * \brief Set the stream group for a stream
 *
 * \param stream The stream
 * \param group The group the stream is part of
 *
 * \since 15.2.0
 */
void ast_stream_set_group(struct ast_stream *stream, int group);

#endif /* _AST_STREAM_H */
