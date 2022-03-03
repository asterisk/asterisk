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
 * \brief Internal macro to assist converting enums to strings
 * \internal
 * \since 18
 *
 * This macro checks that _value is in the bounds
 * of the enum/string map.
 */
#define _stream_maps_to_str(_mapname, _value) \
({ \
	const char *_rtn = ""; \
	if (ARRAY_IN_BOUNDS(_value, _mapname)) { \
		_rtn = _mapname[_value]; \
	} \
	_rtn; \
})

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
	/*!
	 * \brief Sentinel
	 */
	AST_STREAM_STATE_END
};

/*!
 * \brief Stream state enum to string map
 * \internal
 * \since 18
 */
extern const char *ast_stream_state_map[AST_STREAM_STATE_END];

/*!
 * \brief Safely get the name of a stream state
 * \since 18
 *
 * \param stream_state One of enum ast_stream_state
 * \return A constant string with the name of the state or an empty string
 * if an invalid value was passed in.
 */
#define ast_stream_state_to_str(stream_state) _stream_maps_to_str(ast_stream_state_map, stream_state)

/*!
 * \brief Advanced Codec Negotiation Preferences
 * \since 18
 */

/*!
 * \brief The preference parameters themselves
 * \since 18
 */
enum ast_stream_codec_negotiation_params {
	CODEC_NEGOTIATION_PARAM_UNSPECIFIED = 0,
	/*! Which of the lists to "prefer" */
	CODEC_NEGOTIATION_PARAM_PREFER,
	/*! "operation" to perform */
	CODEC_NEGOTIATION_PARAM_OPERATION,
	/*! "keep" all or only first */
	CODEC_NEGOTIATION_PARAM_KEEP,
	/*! Allow or prevent "transcode" */
	CODEC_NEGOTIATION_PARAM_TRANSCODE,
	/*! Sentinel */
	CODEC_NEGOTIATION_PARAM_END,
};

/*!
 * \brief The "prefer" values
 * \since 18
 */
enum ast_stream_codec_negotiation_prefs_prefer_values {
	CODEC_NEGOTIATION_PREFER_UNSPECIFIED = 0,
	/*! Prefer the "pending" list */
	CODEC_NEGOTIATION_PREFER_PENDING,
	/*! Prefer the "configured" list */
	CODEC_NEGOTIATION_PREFER_CONFIGURED,
	/*! Sentinel */
	CODEC_NEGOTIATION_PREFER_END,
};

/*!
 * \brief The "operation" values
 * \since 18
 */
enum ast_stream_codec_negotiation_prefs_operation_values {
	CODEC_NEGOTIATION_OPERATION_UNSPECIFIED = 0,
	/*! "intersect": only those codecs that appear in both lists */
	CODEC_NEGOTIATION_OPERATION_INTERSECT,
	/*! "union": all codecs in both lists */
	CODEC_NEGOTIATION_OPERATION_UNION,
	/*! "only_preferred": only the codecs in the preferred list */
	CODEC_NEGOTIATION_OPERATION_ONLY_PREFERRED,
	/*! "only_nonpreferred": only the codecs in the non-preferred list */
	CODEC_NEGOTIATION_OPERATION_ONLY_NONPREFERRED,
	/*! Sentinel */
	CODEC_NEGOTIATION_OPERATION_END,
};

/*!
 * \brief The "keep" values
 * \since 18
 */
enum ast_stream_codec_negotiation_prefs_keep_values {
	CODEC_NEGOTIATION_KEEP_UNSPECIFIED = 0,
	/*! "keep" all codecs after performing the operation */
	CODEC_NEGOTIATION_KEEP_ALL,
	/*! "keep" only the first codec after performing the operation */
	CODEC_NEGOTIATION_KEEP_FIRST,
	/*! Sentinel */
	CODEC_NEGOTIATION_KEEP_END,
};

/*!
 * \brief The "transcode" values
 * \since 18
 */
enum ast_stream_codec_negotiation_prefs_transcode_values {
	CODEC_NEGOTIATION_TRANSCODE_UNSPECIFIED = 0,
	/*! "allow" transcoding */
	CODEC_NEGOTIATION_TRANSCODE_ALLOW,
	/*! "prevent" transcoding */
	CODEC_NEGOTIATION_TRANSCODE_PREVENT,
	/*! Sentinel */
	CODEC_NEGOTIATION_TRANSCODE_END,
};

/*!
 * \brief Preference enum to string map
 * \internal
 * \since 18
 */
extern const char *ast_stream_codec_negotiation_params_map[CODEC_NEGOTIATION_PARAM_END];

/*!
 * \brief "prefer" enum to string map
 * \internal
 * \since 18
 */
extern const char *ast_stream_codec_negotiation_prefer_map[CODEC_NEGOTIATION_PREFER_END];

/*!
 * \brief "operation" enum to string map
 * \internal
 * \since 18
 */
extern const char *ast_stream_codec_negotiation_operation_map[CODEC_NEGOTIATION_OPERATION_END];

/*!
 * \brief "keep" enum to string map
 * \internal
 * \since 18
 */
extern const char *ast_stream_codec_negotiation_keep_map[CODEC_NEGOTIATION_KEEP_END];

/*!
 * \brief "transcode" state enum to string map
 * \internal
 * \since 18
 */
extern const char *ast_stream_codec_negotiation_transcode_map[CODEC_NEGOTIATION_TRANSCODE_END];

/*!
 * \brief Safely get the name of a preference parameter
 * \since 18
 *
 * \param value One of enum \ref ast_stream_codec_negotiation_params
 * \return A constant string with the name of the preference or an empty string
 * if an invalid value was passed in.
 */
#define ast_stream_codec_param_to_str(value) _stream_maps_to_str(ast_stream_codec_negotiation_params_map, value)

/*!
 * \brief Safely get the name of a "prefer" parameter value
 * \since 18
 *
 * \param value One of enum \ref ast_stream_codec_negotiation_prefs_prefer_values
 * \return A constant string with the name of the value or an empty string
 * if an invalid value was passed in.
 */
#define ast_stream_codec_prefer_to_str(value) _stream_maps_to_str(ast_stream_codec_negotiation_prefer_map, value)

/*!
 * \brief Safely get the name of an "operation" parameter value
 * \since 18
 *
 * \param value One of enum \ref ast_stream_codec_negotiation_prefs_operation_values
 * \return A constant string with the name of the value or an empty string
 * if an invalid value was passed in.
 */
#define ast_stream_codec_operation_to_str(value) _stream_maps_to_str(ast_stream_codec_negotiation_operation_map, value)

/*!
 * \brief Safely get the name of a "keep" parameter value
 * \since 18
 *
 * \param value One of enum \ref ast_stream_codec_negotiation_prefs_keep_values
 * \return A constant string with the name of the value or an empty string
 * if an invalid value was passed in.
 */
#define ast_stream_codec_keep_to_str(value) _stream_maps_to_str(ast_stream_codec_negotiation_keep_map, value)

/*!
 * \brief Safely get the name of a "transcode" parameter value
 * \since 18
 *
 * \param value One of enum \ref ast_stream_codec_negotiation_prefs_transcode_values
 * \return A constant string with the name of the value or an empty string
 * if an invalid value was passed in.
 */
#define ast_stream_codec_transcode_to_str(value) _stream_maps_to_str(ast_stream_codec_negotiation_transcode_map, value)

/*!
 * \brief
 * \since 18
 *
 * The structure that makes up a codec negotiation preference object
 */
struct ast_stream_codec_negotiation_prefs {
	/*! Which codec list to prefer */
	enum ast_stream_codec_negotiation_prefs_prefer_values prefer;
	/*! The operation to perform on the lists */
	enum ast_stream_codec_negotiation_prefs_operation_values operation;
	/*! What to keep after the operation is performed */
	enum ast_stream_codec_negotiation_prefs_keep_values keep;
	/*! To allow or prevent transcoding */
	enum ast_stream_codec_negotiation_prefs_transcode_values transcode;
};

/*!
 * \brief Define for allocating buffer space for to_str() functions
 * \since 18
 */
#define AST_STREAM_MAX_CODEC_PREFS_LENGTH (128)

/*!
 * \brief Return a string representing the codec preferences
 * \since 18
 *
 * This function can be used for debugging purposes but is also
 * used in pjsip_configuration as a sorcery parameter handler
 *
 * \param prefs A pointer to a ast_stream_codec_negotiation_prefs structure
 * \param buf A pointer to an ast_str* used for the output.  See note below.
 *
 * \return the contents of the ast_str as a const char *.
 *
 * \warning No attempt should ever be made to free the returned
 * char * and it should be dup'd if needed after the ast_str is freed.
 *
 * \note
 * buf can't be NULL but it CAN contain a NULL value.  If so, a new
 * ast_str will be allocated and the value of buf updated with a pointer
 * to it.  Whether the caller supplies the ast_str or it's allocated by
 * this function, it's the caller's responsibility to free it.
 *
 * Sample output:
 * "prefer: configured, operation: union, keep:all, transcode:prevent"
 */
const char *ast_stream_codec_prefs_to_str(const struct ast_stream_codec_negotiation_prefs *prefs,
	struct ast_str **buf);

/*!
 * \brief Parses a string representing the codec prefs into a ast_stream_codec_negotiation_pref structure
 * \since 18
 *
 * This function is mainly used by pjsip_configuration as a sorcery parameter handler.
 *
 * \param pref_string A string in the format described by ast_stream_codec_prefs_to_str().
 * \param prefs Pointer to a ast_stream_codec_negotiation_prefs structure to receive the parsed values.
 * \param error_message An optional ast_str** into which parsing errors will be placed.
 *
 * \retval 0 if success
 * \retval -1 if failed
 *
 * \details
 * Whitespace around the ':' and ',' separators is ignored and the parameters
 * can be specified in any order.  Parameters missing in the input string
 * will have their values set to the appropriate *_UNSPECIFIED value and will not
 * be considered an error.  It's up to the caller to decide whether set a default
 * value, return an error, etc.
 *
 * Sample input:
 * "prefer : configured , operation: union,keep:all, transcode:prevent"
 */
int ast_stream_codec_prefs_parse(const char *pref_string, struct ast_stream_codec_negotiation_prefs *prefs,
	struct ast_str **error_message);

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
 * \param name Optional for cloned stream. If NULL, then existing stream's name is copied.
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
const struct ast_format_cap *ast_stream_get_formats(const struct ast_stream *stream);

/*!
 * \brief Get a string representing the stream for debugging/display purposes
 * \since 18
 *
 * \param stream A stream
 * \param buf A pointer to an ast_str* used for the output.
 *
 * \retval "" (empty string) if either buf or *buf are NULL
 * \retval "(null stream)" if *stream was NULL
 * \return \<stream_representation\> otherwise
 *
 * \warning No attempt should ever be made to free the returned
 * char * and it should be dup'd if needed after the ast_str is freed.
 *
 * \details
 *
 * Return format:
 * \verbatim <name>:<media_type>:<stream_state> (formats) \endverbatim
 *
 * Sample return:
 * \verbatim "audio:audio:sendrecv (ulaw,g722)" \endverbatim
 *
 */
const char *ast_stream_to_str(const struct ast_stream *stream, struct ast_str **buf);

/*!
 * \brief Get a stack allocated string representing the stream for debugging/display purposes
 *
 * \param __stream A stream
 *
 * \return A stack allocated pointer to a string representing the stream.
 *
 * \warning No attempt should ever be made to free the returned
 * char* as it is allocated from the stack.
 *
 */
#define ast_stream_to_stra(__stream) ast_str_tmp(128, ast_stream_to_str(__stream, &STR_TMP))

/*!
 * \brief Get the count of the current negotiated formats of a stream
 *
 * \param stream The media stream
 *
 * \return The count of negotiated formats
 *
 * \since 18
 */
int ast_stream_get_format_count(const struct ast_stream *stream);

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
 * \return An ast_variable list of the metadata key/value pairs.
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
 * \brief Create a resolved stream from 2 streams
 * \since 18
 *
 * \param pending_stream The "live" stream created from an SDP,
 *        passed through the core, or used to create an SDP.
 * \param configured_stream The static stream used to validate the pending stream.
 * \param prefs A pointer to an ast_stream_codec_negotiation_prefs structure.
 * \param error_message If supplied, error messages will be appended.
 *
 * \details
 * The resulting stream will contain all of the attributes and metadata of the
 * pending stream but will contain only the formats that passed the validation
 * specified by the ast_stream_codec_negotiation_prefs structure.  This may mean
 * that the stream's format_caps will be empty.  It's up to the caller to determine
 * what to do with the stream in that case.  I.E. Free it, set it to the
 * REMOVED state, etc.  A stream will always be returned unless there was
 * some catastrophic allocation failure.
 *
 * \retval NULL if there was some allocation failure.
 * \return A new, resolved stream.
 *
 */
struct ast_stream *ast_stream_create_resolved(struct ast_stream *pending_stream,
	struct ast_stream *configured_stream, struct ast_stream_codec_negotiation_prefs *prefs,
	struct ast_str **error_message);

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
 * \return The position of the stream in the topology (-1 on error)
 *
 * \since 15
 *
 * \note If the stream's name is empty, it'll be set to \<stream_type\>-\<position\>
 */
int ast_stream_topology_append_stream(struct ast_stream_topology *topology,
	struct ast_stream *stream);

/*!
 * \brief Get the number of streams in a topology
 *
 * \param topology The topology of streams
 *
 * \return The number of streams (-1 on error)
 *
 * \since 15
 */
int ast_stream_topology_get_count(const struct ast_stream_topology *topology);

/*!
 * \brief Get the number of active (non-REMOVED) streams in a topology
 *
 * \param topology The topology of streams
 *
 * \return the number of active streams
 *
 * \since 18
 */
int ast_stream_topology_get_active_count(const struct ast_stream_topology *topology);


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
 *
 * \note If the stream's name is empty, it'll be set to \<stream_type\>-\<position\>
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
 */
int ast_stream_topology_del_stream(struct ast_stream_topology *topology,
	unsigned int position);

/*!
 * \brief A helper function that, given a format capabilities structure,
 * creates a topology and separates the media types in format_cap into
 * separate streams.
 *
 * \param cap The format capabilities structure (NULL creates an empty topology)
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
 * \retval non-NULL success (the resulting format caps must be unreffed by the caller)
 * \retval NULL failure
 *
 * \note The stream topology is NOT altered by this function.
 *
 * \since 15
 */
struct ast_format_cap *ast_stream_topology_get_formats(
	struct ast_stream_topology *topology);

/*!
 * \brief Get a string representing the topology for debugging/display purposes
 * \since 18
 *
 * \param topology A stream topology
 * \param buf A pointer to an ast_str* used for the output.
 *
 * \retval "" (empty string) if either buf or *buf are NULL
 * \retval "(null topology)" if *topology was NULL
 * \return \<topology_representation\> otherwise
 *
 * \warning No attempt should ever be made to free the returned
 * char * and it should be dup'd if needed after the ast_str is freed.
  *
 * Return format:
 * \verbatim <final>? <stream> ... \endverbatim
 *
 * Sample return:
 * \verbatim "final <audio:audio:sendrecv (ulaw,g722)> <video:video:sendonly (h264)>" \endverbatim
 *
 */
const char *ast_stream_topology_to_str(const struct ast_stream_topology *topology, struct ast_str **buf);

/*!
 * \brief Create a format capabilities structure containing all the formats from all the streams
 *        of a particular type in the topology.
 * \since 18
 *
 * \details
 * A helper function that, given a stream topology and a media type, creates a format
 * capabilities structure containing all formats from all active streams with the particular type.
 *
 * \param topology The topology of streams
 * \param type The media type
 *
 * \retval non-NULL success (the resulting format caps must be unreffed by the caller)
 * \retval NULL failure
 *
 * \note The stream topology is NOT altered by this function.
 *
 */
struct ast_format_cap *ast_stream_topology_get_formats_by_type(
    struct ast_stream_topology *topology, enum ast_media_type type);

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

/*!
 * \brief Create a resolved stream topology from 2 topologies
 * \since 18
 *
 * \param pending_topology The "live" topology created from an SDP,
 *        passed through the core, or used to create an SDP.
 * \param validation_topology The static topology used to validate the pending topology.
 *        It MUST have only 1 stream per media type.
 * \param prefs A pointer to an ast_stream_codec_negotiation_prefs structure.
 * \param error_message If supplied, error messages will be appended.
 *
 * \details
 * The streams in the resolved topology will contain all of the attributes
 * of the corresponding stream from the pending topology. It's format_caps
 * however will contain only the formats that passed the validation
 * specified by the ast_stream_codec_negotiation_prefs structure.  This may
 * mean that some of the streams format_caps will be empty.  If that's the case,
 * the stream will be in a REMOVED state.  With those rules in mind,
 * a resolved topology will always be returned (unless there's some catastrophic
 * allocation failure) and the resolved topology is guaranteed to have the same
 * number of streams, in the same order, as the pending topology.
 *
 * \retval NULL if there was some allocation failure.
 * \return The joint topology.
 */
struct ast_stream_topology *ast_stream_topology_create_resolved(
	struct ast_stream_topology *pending_topology, struct ast_stream_topology *validation_topology,
	struct ast_stream_codec_negotiation_prefs *prefs,
	struct ast_str **error_message);

/*!
 * \brief Get a stack allocated string representing the topology for debugging/display purposes
 *
 * \param __topology A topology
 *
 * \return A stack allocated pointer to a string representing the topology.
 *
 * \warning No attempt should ever be made to free the returned
 * char* as it is allocated from the stack.
 *
 */
#define ast_stream_topology_to_stra(__topology) ast_str_tmp(256, ast_stream_topology_to_str(__topology, &STR_TMP))

#endif /* _AST_STREAM_H */
