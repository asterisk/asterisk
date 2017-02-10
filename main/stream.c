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

/*! \file
 *
 * \brief Media Stream API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/logger.h"
#include "asterisk/stream.h"
#include "asterisk/strings.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"

struct ast_stream {
	/*!
	 * \brief The type of media the stream is handling
	 */
	enum ast_media_type type;

	/*!
	 * \brief The position of the stream in the topology
	 */
	unsigned int position;

	/*!
	 * \brief Current formats negotiated on the stream
	 */
	struct ast_format_cap *formats;

	/*!
	 * \brief The current state of the stream
	 */
	enum ast_stream_state state;

	/*!
	 * \brief Name for the stream within the context of the channel it is on
	 */
	char name[0];
};

struct ast_stream_topology {
    /*!
     * \brief A vector of all the streams in this topology
     */
    AST_VECTOR(, struct ast_stream *) streams;
};

struct ast_stream *ast_stream_create(const char *name, enum ast_media_type type)
{
	struct ast_stream *stream;

	stream = ast_calloc(1, sizeof(*stream) + strlen(S_OR(name, "")) + 1);
	if (!stream) {
		return NULL;
	}

	stream->type = type;
	stream->state = AST_STREAM_STATE_INACTIVE;
	strcpy(stream->name, S_OR(name, "")); /* Safe */

	return stream;
}

struct ast_stream *ast_stream_clone(const struct ast_stream *stream)
{
	struct ast_stream *new_stream;
	size_t stream_size;

	if (!stream) {
		return NULL;
	}

	stream_size = sizeof(*stream) + strlen(stream->name) + 1;
	new_stream = ast_calloc(1, stream_size);
	if (!new_stream) {
		return NULL;
	}

	memcpy(new_stream, stream, stream_size);
	if (new_stream->formats) {
		ao2_ref(new_stream->formats, +1);
	}

	return new_stream;
}

void ast_stream_destroy(struct ast_stream *stream)
{
	if (!stream) {
		return;
	}

	ao2_cleanup(stream->formats);
	ast_free(stream);
}

const char *ast_stream_get_name(const struct ast_stream *stream)
{
	return stream ? stream->name : NULL;
}

enum ast_media_type ast_stream_get_type(const struct ast_stream *stream)
{
	return stream ? stream->type : AST_MEDIA_TYPE_UNKNOWN;
}

void ast_stream_set_type(struct ast_stream *stream, enum ast_media_type type)
{
	if (!stream) {
		return;
	}

	stream->type = type;
}

struct ast_format_cap *ast_stream_get_formats(const struct ast_stream *stream)
{
	return stream ? stream->formats : NULL;
}

void ast_stream_set_formats(struct ast_stream *stream, struct ast_format_cap *caps)
{
	if (!stream) {
		return;
	}

	ao2_cleanup(stream->formats);
	stream->formats = ao2_bump(caps);
}

enum ast_stream_state ast_stream_get_state(const struct ast_stream *stream)
{
	return stream ? stream->state : AST_STREAM_STATE_UNKNOWN;
}

void ast_stream_set_state(struct ast_stream *stream, enum ast_stream_state state)
{
	if (!stream) {
		return;
	}

	stream->state = state;
}

int ast_stream_get_position(const struct ast_stream *stream)
{
	return stream ? stream->position : -1;
}

#define TOPOLOGY_INITIAL_STREAM_COUNT 2
struct ast_stream_topology *ast_stream_topology_create(void)
{
	struct ast_stream_topology *topology;

	topology = ast_calloc(1, sizeof(*topology));
	if (!topology) {
		return NULL;
	}

	if (AST_VECTOR_INIT(&topology->streams, TOPOLOGY_INITIAL_STREAM_COUNT)) {
		ast_free(topology);
		topology = NULL;
	}

	return topology;
}

struct ast_stream_topology *ast_stream_topology_clone(
	const struct ast_stream_topology *topology)
{
	struct ast_stream_topology *new_topology;
	int i;

	if (!topology) {
		return NULL;
	}

	new_topology = ast_stream_topology_create();
	if (!new_topology) {
		return NULL;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&topology->streams); i++) {
		struct ast_stream *stream =
			ast_stream_clone(AST_VECTOR_GET(&topology->streams, i));

		if (!stream || AST_VECTOR_APPEND(&new_topology->streams, stream)) {
			ast_stream_destroy(stream);
			ast_stream_topology_destroy(new_topology);
			return NULL;
		}
	}

	return new_topology;
}

void ast_stream_topology_destroy(struct ast_stream_topology *topology)
{
	if (!topology) {
		return;
	}

	AST_VECTOR_CALLBACK_VOID(&topology->streams, ast_stream_destroy);
	AST_VECTOR_FREE(&topology->streams);
	ast_free(topology);
}

int ast_stream_topology_append_stream(struct ast_stream_topology *topology, struct ast_stream *stream)
{
	if (!topology || !stream) {
		return -1;
	}

	if (AST_VECTOR_APPEND(&topology->streams, stream)) {
		return -1;
	}

	return AST_VECTOR_SIZE(&topology->streams) - 1;
}

int ast_stream_topology_get_count(const struct ast_stream_topology *topology)
{
	return topology ? AST_VECTOR_SIZE(&topology->streams) : -1;
}

struct ast_stream *ast_stream_topology_get_stream(
	const struct ast_stream_topology *topology, unsigned int stream_num)
{
	return topology ? AST_VECTOR_GET(&topology->streams, stream_num) : NULL;
}

int ast_stream_topology_set_stream(struct ast_stream_topology *topology,
	unsigned int position, struct ast_stream *stream)
{
	struct ast_stream *existing_stream;

	if (!topology || !stream || position > AST_VECTOR_SIZE(&topology->streams)) {
		return -1;
	}

	existing_stream = AST_VECTOR_GET(&topology->streams, position);
	ast_stream_destroy(existing_stream);

	if (position == AST_VECTOR_SIZE(&topology->streams)) {
		AST_VECTOR_APPEND(&topology->streams, stream);
		return 0;
	}

	stream->position = position;
	return AST_VECTOR_REPLACE(&topology->streams, position, stream);
}

static char *type_names[] = {
	"AST_MEDIA_TYPE_UNKNOWN",
	"AST_MEDIA_TYPE_AUDIO",
	"AST_MEDIA_TYPE_VIDEO",
	"AST_MEDIA_TYPE_IMAGE",
	"AST_MEDIA_TYPE_TEXT"
};

struct ast_stream_topology *ast_stream_topology_create_from_format_cap(
	struct ast_format_cap *cap)
{
	struct ast_stream_topology *topology;
	enum ast_media_type type;

	if (!cap) {
		return NULL;
	}

	topology = ast_stream_topology_create();
	if (!topology) {
		return NULL;
	}

	for (type = AST_MEDIA_TYPE_UNKNOWN + 1; type < AST_MEDIA_TYPE_END; type++) {
		struct ast_format_cap *new_cap;
		struct ast_stream *stream;

		if (!ast_format_cap_has_type(cap, type)) {
			continue;
		}

		new_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (!new_cap) {
			ast_stream_topology_destroy(topology);
			return NULL;
		}

		ast_format_cap_set_framing(new_cap, ast_format_cap_get_framing(cap));
		if (ast_format_cap_append_from_cap(new_cap, cap, type)) {
			ao2_cleanup(new_cap);
			ast_stream_topology_destroy(topology);
			return NULL;
		}

		stream = ast_stream_create(type_names[type], type);
		if (!stream) {
			ao2_cleanup(new_cap);
			ast_stream_topology_destroy(topology);
			return NULL;
		}
		/* We're transferring the initial ref so no bump needed */
		stream->formats = new_cap;
		stream->state = AST_STREAM_STATE_SENDRECV;
		if (!ast_stream_topology_append_stream(topology, stream)) {
			ast_stream_destroy(stream);
			ast_stream_topology_destroy(topology);
			return NULL;
		}
	}

	return topology;
}
