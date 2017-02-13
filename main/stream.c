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

struct ast_stream {
	/*!
	 * \brief The type of media the stream is handling
	 */
	enum ast_media_type type;

	/*!
	 * \brief Unique number for the stream within the context of the channel it is on
	 */
	unsigned int num;

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

struct ast_stream *ast_stream_create(const char *name, enum ast_media_type type)
{
	struct ast_stream *stream;

	stream = ast_calloc(1, sizeof(*stream) + strlen(S_OR(name, "")) + 1);
	if (!stream) {
		return NULL;
	}

	stream->type = type;
	stream->state = AST_STREAM_STATE_INACTIVE;
	strcpy(stream->name, S_OR(name, ""));

	return stream;
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
	return stream->name;
}

enum ast_media_type ast_stream_get_type(const struct ast_stream *stream)
{
	return stream->type;
}

void ast_stream_set_type(struct ast_stream *stream, enum ast_media_type type)
{
	stream->type = type;
}

struct ast_format_cap *ast_stream_get_formats(const struct ast_stream *stream)
{
	return stream->formats;
}

void ast_stream_set_formats(struct ast_stream *stream, struct ast_format_cap *caps)
{
	ao2_cleanup(stream->formats);
	stream->formats = ao2_bump(caps);
}

enum ast_stream_state ast_stream_get_state(const struct ast_stream *stream)
{
	return stream->state;
}

void ast_stream_set_state(struct ast_stream *stream, enum ast_stream_state state)
{
	stream->state = state;
}

unsigned int ast_stream_get_num(const struct ast_stream *stream)
{
	return stream->num;
}
