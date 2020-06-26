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
 * \brief Media Stream API Unit Tests
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/stream.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/format_cache.h"
#include "asterisk/channel.h"
#include "asterisk/uuid.h"

AST_TEST_DEFINE(stream_create)
{
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_free);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_create";
		info->category = "/main/stream/";
		info->summary = "stream create unit test";
		info->description =
			"Test that creating a stream results in a stream with the expected values";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	stream = ast_stream_alloc("test", AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create media stream given proper arguments\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_state(stream) != AST_STREAM_STATE_INACTIVE) {
		ast_test_status_update(test, "Newly created stream does not have expected inactive stream state\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_type(stream) != AST_MEDIA_TYPE_AUDIO) {
		ast_test_status_update(test, "Newly created stream does not have expected audio media type\n");
		return AST_TEST_FAIL;
	}

	if (strcmp(ast_stream_get_name(stream), "test")) {
		ast_test_status_update(test, "Newly created stream does not have expected name of test\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_create_no_name)
{
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_free);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_create_no_name";
		info->category = "/main/stream/";
		info->summary = "stream create (without a name) unit test";
		info->description =
			"Test that creating a stream with no name works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	stream = ast_stream_alloc(NULL, AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create media stream given proper arguments\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_set_type)
{
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_free);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_set_type";
		info->category = "/main/stream/";
		info->summary = "stream type setting unit test";
		info->description =
			"Test that changing the type of a stream works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	stream = ast_stream_alloc("test", AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create media stream given proper arguments\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_type(stream) != AST_MEDIA_TYPE_AUDIO) {
		ast_test_status_update(test, "Newly created stream does not have expected audio media type\n");
		return AST_TEST_FAIL;
	}

	ast_stream_set_type(stream, AST_MEDIA_TYPE_VIDEO);

	if (ast_stream_get_type(stream) != AST_MEDIA_TYPE_VIDEO) {
		ast_test_status_update(test, "Changed stream does not have expected video media type\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_set_formats)
{
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_free);
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_set_formats";
		info->category = "/main/stream/";
		info->summary = "stream formats setting unit test";
		info->description =
			"Test that changing the formats of a stream works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Failed to create a format capabilities structure for testing\n");
		return AST_TEST_FAIL;
	}

	stream = ast_stream_alloc("test", AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create media stream given proper arguments\n");
		return AST_TEST_FAIL;
	}

	ast_stream_set_formats(stream, caps);

	if (ast_stream_get_formats(stream) != caps) {
		ast_test_status_update(test, "Changed stream does not have expected formats\n");
		return AST_TEST_FAIL;
	}

	ast_stream_set_formats(stream, NULL);

	if (ast_stream_get_formats(stream)) {
		ast_test_status_update(test, "Retrieved formats from stream despite removing them\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_set_state)
{
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_free);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_set_state";
		info->category = "/main/stream/";
		info->summary = "stream state setting unit test";
		info->description =
			"Test that changing the state of a stream works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	stream = ast_stream_alloc("test", AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create media stream given proper arguments\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_state(stream) != AST_STREAM_STATE_INACTIVE) {
		ast_test_status_update(test, "Newly created stream does not have expected inactive stream state\n");
		return AST_TEST_FAIL;
	}

	ast_stream_set_state(stream, AST_STREAM_STATE_SENDRECV);

	if (ast_stream_get_state(stream) != AST_STREAM_STATE_SENDRECV) {
		ast_test_status_update(test, "Changed stream does not have expected sendrecv state\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_metadata)
{
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_free);
	char track_label[AST_UUID_STR_LEN + 1];
	const char *stream_track_label;
	int rc;

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_metadata";
		info->category = "/main/stream/";
		info->summary = "stream metadata unit test";
		info->description =
			"Test that metadata operations on a stream works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	stream = ast_stream_alloc("test", AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create media stream given proper arguments\n");
		return AST_TEST_FAIL;
	}

	stream_track_label = ast_stream_get_metadata(stream, "AST_STREAM_METADATA_TRACK_LABEL");
	if (stream_track_label) {
		ast_test_status_update(test, "New stream HAD a track label\n");
		return AST_TEST_FAIL;
	}

	ast_uuid_generate_str(track_label, sizeof(track_label));
	rc = ast_stream_set_metadata(stream, "AST_STREAM_METADATA_TRACK_LABEL", track_label);
	if (rc != 0) {
		ast_test_status_update(test, "Failed to add track label\n");
		return AST_TEST_FAIL;
	}

	stream_track_label = ast_stream_get_metadata(stream, "AST_STREAM_METADATA_TRACK_LABEL");
	if (!stream_track_label) {
		ast_test_status_update(test, "Changed stream does not have a track label\n");
		return AST_TEST_FAIL;
	}

	if (strcmp(stream_track_label, track_label) != 0) {
		ast_test_status_update(test, "Changed stream did not return same track label\n");
		return AST_TEST_FAIL;
	}

	rc = ast_stream_set_metadata(stream, "AST_STREAM_METADATA_TRACK_LABEL", NULL);
	if (rc != 0) {
		ast_test_status_update(test, "Failed to remove track label\n");
		return AST_TEST_FAIL;
	}

	stream_track_label = ast_stream_get_metadata(stream, "AST_STREAM_METADATA_TRACK_LABEL");
	if (stream_track_label) {
		ast_test_status_update(test, "Changed stream still had a track label after we removed it\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_topology_create)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_create";
		info->category = "/main/stream/";
		info->summary = "stream topology creation unit test";
		info->description =
			"Test that creating a stream topology works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topology = ast_stream_topology_alloc();
	if (!topology) {
		ast_test_status_update(test, "Failed to create media stream topology\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_topology_clone)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);
	RAII_VAR(struct ast_stream_topology *, cloned, NULL, ast_stream_topology_free);
	struct ast_stream *audio_stream, *video_stream;
	char audio_track_label[AST_UUID_STR_LEN + 1];
	char video_track_label[AST_UUID_STR_LEN + 1];
	const char *original_track_label;
	const char *cloned_track_label;
	int rc;

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_clone";
		info->category = "/main/stream/";
		info->summary = "stream topology cloning unit test";
		info->description =
			"Test that cloning a stream topology results in a clone with the same contents";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topology = ast_stream_topology_alloc();
	if (!topology) {
		ast_test_status_update(test, "Failed to create media stream topology\n");
		return AST_TEST_FAIL;
	}

	audio_stream = ast_stream_alloc("audio", AST_MEDIA_TYPE_AUDIO);
	if (!audio_stream) {
		ast_test_status_update(test, "Failed to create an audio stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	ast_uuid_generate_str(audio_track_label, sizeof(audio_track_label));
	rc = ast_stream_set_metadata(audio_stream, "AST_STREAM_METADATA_TRACK_LABEL", audio_track_label);
	if (rc != 0) {
		ast_test_status_update(test, "Failed to add track label\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, audio_stream) == -1) {
		ast_test_status_update(test, "Failed to append valid audio stream to stream topology\n");
		ast_stream_free(audio_stream);
		return AST_TEST_FAIL;
	}

	video_stream = ast_stream_alloc("video", AST_MEDIA_TYPE_VIDEO);
	if (!video_stream) {
		ast_test_status_update(test, "Failed to create a video stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	ast_uuid_generate_str(video_track_label, sizeof(video_track_label));
	rc = ast_stream_set_metadata(video_stream, "AST_STREAM_METADATA_TRACK_LABEL", video_track_label);
	if (rc != 0) {
		ast_test_status_update(test, "Failed to add track label\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, video_stream) == -1) {
		ast_test_status_update(test, "Failed to append valid video stream to stream topology\n");
		ast_stream_free(video_stream);
		return AST_TEST_FAIL;
	}

	cloned = ast_stream_topology_clone(topology);
	if (!cloned) {
		ast_test_status_update(test, "Failed to clone a perfectly good stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_count(cloned) != ast_stream_topology_get_count(topology)) {
		ast_test_status_update(test, "Cloned stream topology does not contain same number of streams as original\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_type(ast_stream_topology_get_stream(cloned, 0)) != ast_stream_get_type(ast_stream_topology_get_stream(topology, 0))) {
		ast_test_status_update(test, "Cloned audio stream does not contain same type as original\n");
		return AST_TEST_FAIL;
	}

	original_track_label = ast_stream_get_metadata(ast_stream_topology_get_stream(topology, 0),
		"AST_STREAM_METADATA_TRACK_LABEL");
	if (!original_track_label) {
		ast_test_status_update(test, "Original topology stream 0 does not contain metadata\n");
		return AST_TEST_FAIL;
	}
	cloned_track_label = ast_stream_get_metadata(ast_stream_topology_get_stream(cloned, 0),
		"AST_STREAM_METADATA_TRACK_LABEL");
	if (!cloned_track_label) {
		ast_test_status_update(test, "Cloned topology stream 0 does not contain metadata\n");
		return AST_TEST_FAIL;
	}
	if (strcmp(original_track_label, cloned_track_label) != 0) {
		ast_test_status_update(test, "Cloned topology stream 0 track label was not the same as the original\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_type(ast_stream_topology_get_stream(cloned, 1)) != ast_stream_get_type(ast_stream_topology_get_stream(topology, 1))) {
		ast_test_status_update(test, "Cloned video stream does not contain same type as original\n");
		return AST_TEST_FAIL;
	}

	original_track_label = ast_stream_get_metadata(ast_stream_topology_get_stream(topology, 1),
		"AST_STREAM_METADATA_TRACK_LABEL");
	if (!original_track_label) {
		ast_test_status_update(test, "Original topology stream 1 does not contain metadata\n");
		return AST_TEST_FAIL;
	}
	cloned_track_label = ast_stream_get_metadata(ast_stream_topology_get_stream(cloned, 1),
		"AST_STREAM_METADATA_TRACK_LABEL");
	if (!cloned_track_label) {
		ast_test_status_update(test, "Cloned topology stream 1 does not contain metadata\n");
		return AST_TEST_FAIL;
	}
	if (strcmp(original_track_label, cloned_track_label) != 0) {
		ast_test_status_update(test, "Cloned topology stream 1 track label was not the same as the original\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_topology_append_stream)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);
	struct ast_stream *audio_stream, *video_stream;
	int position;

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_append_stream";
		info->category = "/main/stream/";
		info->summary = "stream topology stream appending unit test";
		info->description =
			"Test that appending streams to a stream topology works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topology = ast_stream_topology_alloc();
	if (!topology) {
		ast_test_status_update(test, "Failed to create media stream topology\n");
		return AST_TEST_FAIL;
	}

	audio_stream = ast_stream_alloc("audio", AST_MEDIA_TYPE_AUDIO);
	if (!audio_stream) {
		ast_test_status_update(test, "Failed to create an audio stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	position = ast_stream_topology_append_stream(topology, audio_stream);
	if (position == -1) {
		ast_test_status_update(test, "Failed to append valid audio stream to stream topology\n");
		ast_stream_free(audio_stream);
		return AST_TEST_FAIL;
	} else if (position != 0) {
		ast_test_status_update(test, "Appended audio stream to stream topology but position is '%d' instead of 0\n",
			position);
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_count(topology) != 1) {
		ast_test_status_update(test, "Appended an audio stream to the stream topology but stream count is '%d' on it, not 1\n",
			ast_stream_topology_get_count(topology));
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_stream(topology, 0) != audio_stream) {
		ast_test_status_update(test, "Appended an audio stream to the stream topology but returned stream doesn't match\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_position(audio_stream) != 0) {
		ast_test_status_update(test, "Appended audio stream says it is at position '%d' instead of 0\n",
			ast_stream_get_position(audio_stream));
		return AST_TEST_FAIL;
	}

	video_stream = ast_stream_alloc("video", AST_MEDIA_TYPE_VIDEO);
	if (!video_stream) {
		ast_test_status_update(test, "Failed to create a video stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	position = ast_stream_topology_append_stream(topology, video_stream);
	if (position == -1) {
		ast_test_status_update(test, "Failed to append valid video stream to stream topology\n");
		ast_stream_free(video_stream);
		return AST_TEST_FAIL;
	} else if (position != 1) {
		ast_test_status_update(test, "Appended video stream to stream topology but position is '%d' instead of 1\n",
			position);
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_count(topology) != 2) {
		ast_test_status_update(test, "Appended a video stream to the stream topology but stream count is '%d' on it, not 2\n",
			ast_stream_topology_get_count(topology));
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_stream(topology, 1) != video_stream) {
		ast_test_status_update(test, "Appended a video stream to the stream topology but returned stream doesn't match\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_position(video_stream) != 1) {
		ast_test_status_update(test, "Appended video stream says it is at position '%d' instead of 1\n",
			ast_stream_get_position(video_stream));
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_topology_set_stream)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);
	struct ast_stream *audio_stream, *video_stream;

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_set_stream";
		info->category = "/main/stream/";
		info->summary = "stream topology stream setting unit test";
		info->description =
			"Test that setting streams at a specific position in a topology works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topology = ast_stream_topology_alloc();
	if (!topology) {
		ast_test_status_update(test, "Failed to create media stream topology\n");
		return AST_TEST_FAIL;
	}

	audio_stream = ast_stream_alloc("audio", AST_MEDIA_TYPE_AUDIO);
	if (!audio_stream) {
		ast_test_status_update(test, "Failed to create an audio stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_set_stream(topology, 0, audio_stream)) {
		ast_test_status_update(test, "Failed to set an audio stream to a position where it is permitted\n");
		ast_stream_free(audio_stream);
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_count(topology) != 1) {
		ast_test_status_update(test, "Set an audio stream on the stream topology but stream count is '%d' on it, not 1\n",
			ast_stream_topology_get_count(topology));
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_stream(topology, 0) != audio_stream) {
		ast_test_status_update(test, "Set an audio stream on the stream topology but returned stream doesn't match\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_position(audio_stream) != 0) {
		ast_test_status_update(test, "Set audio stream says it is at position '%d' instead of 0\n",
			ast_stream_get_position(audio_stream));
		return AST_TEST_FAIL;
	}

	video_stream = ast_stream_alloc("video", AST_MEDIA_TYPE_VIDEO);
	if (!video_stream) {
		ast_test_status_update(test, "Failed to create a video stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_set_stream(topology, 0, video_stream)) {
		ast_test_status_update(test, "Failed to set a video stream to a position where it is permitted\n");
		ast_stream_free(video_stream);
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_count(topology) != 1) {
		ast_test_status_update(test, "Set a video stream on the stream topology but stream count is '%d' on it, not 1\n",
			ast_stream_topology_get_count(topology));
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_stream(topology, 0) != video_stream) {
		ast_test_status_update(test, "Set a video stream on the stream topology but returned stream doesn't match\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_position(video_stream) != 0) {
		ast_test_status_update(test, "Set video stream says it is at position '%d' instead of 0\n",
			ast_stream_get_position(video_stream));
		return AST_TEST_FAIL;
	}

	audio_stream = ast_stream_alloc("audio", AST_MEDIA_TYPE_AUDIO);
	if (!audio_stream) {
		ast_test_status_update(test, "Failed to create an audio stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_set_stream(topology, 1, audio_stream)) {
		ast_test_status_update(test, "Failed to set an audio stream to a position where it is permitted\n");
		ast_stream_free(audio_stream);
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_count(topology) != 2) {
		ast_test_status_update(test, "Set an audio stream on the stream topology but stream count is '%d' on it, not 2\n",
			ast_stream_topology_get_count(topology));
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_stream(topology, 1) != audio_stream) {
		ast_test_status_update(test, "Set an audio stream on the stream topology but returned stream doesn't match\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_position(audio_stream) != 1) {
		ast_test_status_update(test, "Set audio stream says it is at position '%d' instead of 1\n",
			ast_stream_get_position(audio_stream));
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static int check_stream_positions(struct ast_test *test, const struct ast_stream_topology *topology)
{
	const struct ast_stream *stream;
	int idx;
	int pos;
	enum ast_media_type type;

	for (idx = 0; idx < ast_stream_topology_get_count(topology); ++idx) {
		stream = ast_stream_topology_get_stream(topology, idx);
		pos = ast_stream_get_position(stream);
		if (idx != pos) {
			type = ast_stream_get_type(stream);
			ast_test_status_update(test, "Failed: '%s' stream says it is at position %d instead of %d\n",
				ast_codec_media_type2str(type), pos, idx);
			return -1;
		}
	}
	return 0;
}

AST_TEST_DEFINE(stream_topology_del_stream)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);
	struct ast_stream *stream;
	enum ast_media_type type;
	int idx;

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_del_stream";
		info->category = "/main/stream/";
		info->summary = "stream topology stream delete unit test";
		info->description =
			"Test that deleting streams at a specific position in a topology works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topology = ast_stream_topology_alloc();
	if (!topology) {
		ast_test_status_update(test, "Failed to create media stream topology\n");
		return AST_TEST_FAIL;
	}

	/* Create streams */
	for (type = AST_MEDIA_TYPE_UNKNOWN; type < AST_MEDIA_TYPE_END; ++type) {
		stream = ast_stream_alloc(ast_codec_media_type2str(type), type);
		if (!stream) {
			ast_test_status_update(test, "Failed to create '%s' stream for testing stream topology\n",
				ast_codec_media_type2str(type));
			return AST_TEST_FAIL;
		}
		if (ast_stream_topology_append_stream(topology, stream) == -1) {
			ast_test_status_update(test, "Failed to append '%s' stream to topology\n",
				ast_codec_media_type2str(type));
			ast_stream_free(stream);
			return AST_TEST_FAIL;
		}
	}

	/* Check initial stream positions and types for sanity. */
	type = AST_MEDIA_TYPE_UNKNOWN;
	for (idx = 0; idx < ast_stream_topology_get_count(topology); ++idx, ++type) {
		stream = ast_stream_topology_get_stream(topology, idx);
		if (type != ast_stream_get_type(stream)) {
			ast_test_status_update(test, "Initial topology types failed: Expected:%s Got:%s\n",
				ast_codec_media_type2str(type),
				ast_codec_media_type2str(ast_stream_get_type(stream)));
			return AST_TEST_FAIL;
		}
	}
	if (check_stream_positions(test, topology)) {
		ast_test_status_update(test, "Initial topology positions failed.\n");
		return AST_TEST_FAIL;
	}

	/* Try to delete outside of topology size */
	if (!ast_stream_topology_del_stream(topology, ast_stream_topology_get_count(topology))) {
		ast_test_status_update(test, "Deleting stream outside of topology succeeded!\n");
		return AST_TEST_FAIL;
	}

	/* Try to delete the last topology stream */
	if (ast_stream_topology_del_stream(topology, ast_stream_topology_get_count(topology) - 1)) {
		ast_test_status_update(test, "Failed deleting last stream of topology.\n");
		return AST_TEST_FAIL;
	}
	if (check_stream_positions(test, topology)) {
		ast_test_status_update(test, "Last stream delete topology positions failed.\n");
		return AST_TEST_FAIL;
	}
	stream = ast_stream_topology_get_stream(topology, ast_stream_topology_get_count(topology) - 1);
	type = ast_stream_get_type(stream);
	if (type != AST_MEDIA_TYPE_END - 2) {
		ast_test_status_update(test, "Last stream delete types failed: Expected:%s Got:%s\n",
			ast_codec_media_type2str(AST_MEDIA_TYPE_END - 2),
			ast_codec_media_type2str(type));
		return AST_TEST_FAIL;
	}

	/* Try to delete the second stream in the topology */
	if (ast_stream_topology_del_stream(topology, 1)) {
		ast_test_status_update(test, "Failed deleting second stream in topology.\n");
		return AST_TEST_FAIL;
	}
	if (check_stream_positions(test, topology)) {
		ast_test_status_update(test, "Second stream delete topology positions failed.\n");
		return AST_TEST_FAIL;
	}
	stream = ast_stream_topology_get_stream(topology, 1);
	type = ast_stream_get_type(stream);
	if (type != AST_MEDIA_TYPE_UNKNOWN + 2) {
		ast_test_status_update(test, "Second stream delete types failed: Expected:%s Got:%s\n",
			ast_codec_media_type2str(AST_MEDIA_TYPE_UNKNOWN + 2),
			ast_codec_media_type2str(type));
		return AST_TEST_FAIL;
	}

	/* Try to delete the first stream in the topology */
	if (ast_stream_topology_del_stream(topology, 0)) {
		ast_test_status_update(test, "Failed deleting first stream in topology.\n");
		return AST_TEST_FAIL;
	}
	if (check_stream_positions(test, topology)) {
		ast_test_status_update(test, "First stream delete topology positions failed.\n");
		return AST_TEST_FAIL;
	}
	stream = ast_stream_topology_get_stream(topology, 0);
	type = ast_stream_get_type(stream);
	if (type != AST_MEDIA_TYPE_UNKNOWN + 2) {
		ast_test_status_update(test, "First stream delete types failed: Expected:%s Got:%s\n",
			ast_codec_media_type2str(AST_MEDIA_TYPE_UNKNOWN + 2),
			ast_codec_media_type2str(type));
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_topology_create_from_format_cap)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_create_from_format_cap";
		info->category = "/main/stream/";
		info->summary = "stream topology creation from format capabilities unit test";
		info->description =
			"Test that creating a stream topology from format capabilities results in the expected streams";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ast_format_ulaw, 0)) {
		ast_test_status_update(test, "Failed to append a ulaw format to capabilities for stream topology creation\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ast_format_alaw, 0)) {
		ast_test_status_update(test, "Failed to append an alaw format to capabilities for stream topology creation\n");
		return AST_TEST_FAIL;
	}

	topology = ast_stream_topology_create_from_format_cap(caps);
	if (!topology) {
		ast_test_status_update(test, "Failed to create a stream topology using a perfectly good format capabilities\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_count(topology) != 1) {
		ast_test_status_update(test, "Expected a stream topology with 1 stream but it has %d streams\n",
			ast_stream_topology_get_count(topology));
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_type(ast_stream_topology_get_stream(topology, 0)) != AST_MEDIA_TYPE_AUDIO) {
		ast_test_status_update(test, "Produced stream topology has a single stream of type %s instead of audio\n",
			ast_codec_media_type2str(ast_stream_get_type(ast_stream_topology_get_stream(topology, 0))));
		return AST_TEST_FAIL;
	}

	ast_stream_topology_free(topology);
	topology = NULL;

	ast_format_cap_append(caps, ast_format_h264, 0);

	topology = ast_stream_topology_create_from_format_cap(caps);
	if (!topology) {
		ast_test_status_update(test, "Failed to create a stream topology using a perfectly good format capabilities\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_count(topology) != 2) {
		ast_test_status_update(test, "Expected a stream topology with 2 streams but it has %d streams\n",
			ast_stream_topology_get_count(topology));
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_type(ast_stream_topology_get_stream(topology, 0)) != AST_MEDIA_TYPE_AUDIO) {
		ast_test_status_update(test, "Produced stream topology has a first stream of type %s instead of audio\n",
			ast_codec_media_type2str(ast_stream_get_type(ast_stream_topology_get_stream(topology, 0))));
		return AST_TEST_FAIL;
	}

	if (ast_stream_get_type(ast_stream_topology_get_stream(topology, 1)) != AST_MEDIA_TYPE_VIDEO) {
		ast_test_status_update(test, "Produced stream topology has a second stream of type %s instead of video\n",
			ast_codec_media_type2str(ast_stream_get_type(ast_stream_topology_get_stream(topology, 1))));
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_topology_get_first_stream_by_type)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);
	struct ast_stream *first_stream;
	struct ast_stream *second_stream;
	struct ast_stream *third_stream;
	struct ast_stream *fourth_stream;
	struct ast_stream *fifth_stream;
	struct ast_stream *sixth_stream;

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_get_first_stream_by_type";
		info->category = "/main/stream/";
		info->summary = "stream topology getting first stream by type unit test";
		info->description =
			"Test that getting the first stream by type from a topology actually returns the first stream";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topology = ast_stream_topology_alloc();
	if (!topology) {
		ast_test_status_update(test, "Failed to create media stream topology\n");
		return AST_TEST_FAIL;
	}

	first_stream = ast_stream_alloc("audio", AST_MEDIA_TYPE_AUDIO);
	if (!first_stream) {
		ast_test_status_update(test, "Failed to create an audio stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}
	ast_stream_set_state(first_stream, AST_STREAM_STATE_REMOVED);

	if (ast_stream_topology_append_stream(topology, first_stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_free(first_stream);
		return AST_TEST_FAIL;
	}

	second_stream = ast_stream_alloc("audio2", AST_MEDIA_TYPE_AUDIO);
	if (!second_stream) {
		ast_test_status_update(test, "Failed to create a second audio stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, second_stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_free(second_stream);
		return AST_TEST_FAIL;
	}

	third_stream = ast_stream_alloc("audio3", AST_MEDIA_TYPE_AUDIO);
	if (!third_stream) {
		ast_test_status_update(test, "Failed to create a third audio stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, third_stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_free(third_stream);
		return AST_TEST_FAIL;
	}

	fourth_stream = ast_stream_alloc("video", AST_MEDIA_TYPE_VIDEO);
	if (!fourth_stream) {
		ast_test_status_update(test, "Failed to create a video stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}
	ast_stream_set_state(fourth_stream, AST_STREAM_STATE_REMOVED);

	if (ast_stream_topology_append_stream(topology, fourth_stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_free(fourth_stream);
		return AST_TEST_FAIL;
	}

	fifth_stream = ast_stream_alloc("video2", AST_MEDIA_TYPE_VIDEO);
	if (!fifth_stream) {
		ast_test_status_update(test, "Failed to create a second video stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, fifth_stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_free(fifth_stream);
		return AST_TEST_FAIL;
	}

	sixth_stream = ast_stream_alloc("video3", AST_MEDIA_TYPE_VIDEO);
	if (!sixth_stream) {
		ast_test_status_update(test, "Failed to create a third video stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, sixth_stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_free(sixth_stream);
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_first_stream_by_type(topology, AST_MEDIA_TYPE_AUDIO) != second_stream) {
		ast_test_status_update(test, "Retrieved first audio stream from topology but it is not the correct one\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_first_stream_by_type(topology, AST_MEDIA_TYPE_VIDEO) != fifth_stream) {
		ast_test_status_update(test, "Retrieved first video stream from topology but it is not the correct one\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static const struct ast_channel_tech mock_channel_tech = {
};

AST_TEST_DEFINE(stream_topology_create_from_channel_nativeformats)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	struct ast_channel *mock_channel;
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct ast_str *codec_have_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
	struct ast_str *codec_wanted_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_create_from_channel_nativeformats";
		info->category = "/main/stream/";
		info->summary = "stream topology creation from channel native formats unit test";
		info->description =
			"Test that creating a stream topology from the setting of channel nativeformats results in the expected streams";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ast_format_ulaw, 0)) {
		ast_test_status_update(test, "Failed to append a ulaw format to capabilities for channel nativeformats\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ast_format_alaw, 0)) {
		ast_test_status_update(test, "Failed to append an alaw format to capabilities for channel nativeformats\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ast_format_h264, 0)) {
		ast_test_status_update(test, "Failed to append an h264 format to capabilities for channel nativeformats\n");
		return AST_TEST_FAIL;
	}

	mock_channel = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, "TestChannel");
	if (!mock_channel) {
		ast_test_status_update(test, "Failed to create a mock channel for testing\n");
		return AST_TEST_FAIL;
	}

	ast_channel_tech_set(mock_channel, &mock_channel_tech);
	ast_channel_nativeformats_set(mock_channel, caps);

	if (!ast_channel_get_stream_topology(mock_channel)) {
		ast_test_status_update(test, "Set nativeformats with ulaw, alaw, and h264 on channel but it did not create a topology\n");
		goto end;
	}

	if (ast_stream_topology_get_count(ast_channel_get_stream_topology(mock_channel)) != 2) {
		ast_test_status_update(test, "Set nativeformats on a channel to ulaw, alaw, and h264 and received '%d' streams instead of expected 2\n",
			ast_stream_topology_get_count(ast_channel_get_stream_topology(mock_channel)));
		goto end;
	}

	if (ast_stream_get_type(ast_stream_topology_get_stream(ast_channel_get_stream_topology(mock_channel), 0)) != AST_MEDIA_TYPE_AUDIO) {
		ast_test_status_update(test, "First stream on channel is of %s when it should be audio\n",
			ast_codec_media_type2str(ast_stream_get_type(ast_stream_topology_get_stream(ast_channel_get_stream_topology(mock_channel), 0))));
		goto end;
	}

	ast_format_cap_remove_by_type(caps, AST_MEDIA_TYPE_VIDEO);
	if (!ast_format_cap_identical(ast_stream_get_formats(ast_stream_topology_get_stream(ast_channel_get_stream_topology(mock_channel), 0)), caps)) {
		ast_test_status_update(test, "Formats on audio stream of channel are '%s' when they should be '%s'\n",
			ast_format_cap_get_names(ast_stream_get_formats(ast_stream_topology_get_stream(ast_channel_get_stream_topology(mock_channel), 0)), &codec_have_buf),
			ast_format_cap_get_names(caps, &codec_wanted_buf));
		goto end;
	}

	if (ast_stream_get_type(ast_stream_topology_get_stream(ast_channel_get_stream_topology(mock_channel), 1)) != AST_MEDIA_TYPE_VIDEO) {
		ast_test_status_update(test, "Second stream on channel is of type %s when it should be video\n",
			ast_codec_media_type2str(ast_stream_get_type(ast_stream_topology_get_stream(ast_channel_get_stream_topology(mock_channel), 1))));
		goto end;
	}

	ast_format_cap_remove_by_type(caps, AST_MEDIA_TYPE_AUDIO);

	if (ast_format_cap_append(caps, ast_format_h264, 0)) {
		ast_test_status_update(test, "Failed to append h264 video codec to capabilities for capabilities comparison\n");
		goto end;
	}

	if (!ast_format_cap_identical(ast_stream_get_formats(ast_stream_topology_get_stream(ast_channel_get_stream_topology(mock_channel), 1)), caps)) {
		ast_test_status_update(test, "Formats on video stream of channel are '%s' when they should be '%s'\n",
			ast_format_cap_get_names(ast_stream_get_formats(ast_stream_topology_get_stream(ast_channel_get_stream_topology(mock_channel), 1)), &codec_wanted_buf),
			ast_format_cap_get_names(caps, &codec_wanted_buf));
		goto end;
	}

	res = AST_TEST_PASS;

end:
	ast_channel_unlock(mock_channel);
	ast_hangup(mock_channel);

	return res;
}

struct mock_channel_pvt {
	int mallocd;
	unsigned int wrote;
	unsigned int wrote_stream;
	int stream_num;
	int frame_limit;
	int frame_count;
	int streams;
	int frames_per_read;
	unsigned int indicated_change_request;
	unsigned int indicated_changed;
};

static struct ast_frame *mock_channel_read(struct ast_channel *chan)
{
	struct mock_channel_pvt *pvt = ast_channel_tech_pvt(chan);
	struct ast_frame f = { 0, };
	struct ast_frame *head_frame = NULL;
	struct ast_frame *tail_frame = NULL;
	int i;

	if (pvt->frames_per_read == 0) {
		pvt->frames_per_read = 1;
	}
	for (i = 0; i < pvt->frames_per_read && pvt->frame_count < pvt->frame_limit; i++) {
		struct ast_frame *fr;

		if (pvt->frame_count % 2 == 0) {
			f.frametype = AST_FRAME_VOICE;
			f.subclass.format = ast_format_ulaw;
		} else {
			f.frametype = AST_FRAME_VIDEO;
			f.subclass.format = ast_format_h264;
		}
		f.seqno = pvt->frame_count;
		f.stream_num = pvt->frame_count % pvt->streams;
		pvt->frame_count++;
		fr = ast_frdup(&f);
		if (!head_frame) {
			head_frame = fr;
		} else  {
			tail_frame->frame_list.next = fr;
		}
		tail_frame = fr;
	}

	return(head_frame);
}

static int mock_channel_write(struct ast_channel *chan, struct ast_frame *fr)
{
	struct mock_channel_pvt *pvt = ast_channel_tech_pvt(chan);

	pvt->wrote = 1;

	return 0;
}

static int mock_channel_indicate(struct ast_channel *chan, int condition, const void *data, size_t datalen)
{
	struct mock_channel_pvt *pvt = ast_channel_tech_pvt(chan);

	if (condition == AST_CONTROL_STREAM_TOPOLOGY_REQUEST_CHANGE) {
		pvt->indicated_change_request = 1;
	} else if (condition == AST_CONTROL_STREAM_TOPOLOGY_CHANGED) {
		pvt->indicated_changed = 1;
	}

	return 0;
}

static int mock_channel_write_stream(struct ast_channel *chan, int stream_num, struct ast_frame *fr)
{
	struct mock_channel_pvt *pvt = ast_channel_tech_pvt(chan);

	pvt->wrote_stream = 1;
	pvt->stream_num = stream_num;

	return 0;
}

static const struct ast_channel_tech mock_stream_channel_tech = {
	.read_stream = mock_channel_read,
	.write_stream = mock_channel_write_stream,
};

AST_TEST_DEFINE(stream_topology_channel_set)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);
	struct ast_channel *mock_channel;
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_channel_set";
		info->category = "/main/stream/";
		info->summary = "stream topology setting on a channel unit test";
		info->description =
			"Test that setting a stream topology on a channel works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topology = ast_stream_topology_alloc();
	if (!topology) {
		ast_test_status_update(test, "Failed to create media stream topology\n");
		return AST_TEST_FAIL;
	}

	mock_channel = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, "TestChannel");
	if (!mock_channel) {
		ast_test_status_update(test, "Failed to create a mock channel for testing\n");
		return AST_TEST_FAIL;
	}

	ast_channel_tech_set(mock_channel, &mock_stream_channel_tech);
	ast_channel_set_stream_topology(mock_channel, topology);

	if (ast_channel_get_stream_topology(mock_channel) != topology) {
		ast_test_status_update(test, "Set an explicit stream topology on a channel but the returned one did not match it\n");
		res = AST_TEST_FAIL;
	}

	topology = NULL;
	ast_channel_unlock(mock_channel);
	ast_hangup(mock_channel);

	return res;
}

static int mock_channel_hangup(struct ast_channel *chan)
{
	struct mock_channel_pvt *pvt = ast_channel_tech_pvt(chan);

	if (pvt->mallocd) {
		ast_free(pvt);
	}

	ast_channel_tech_pvt_set(chan, NULL);
	return 0;
}

static const struct ast_channel_tech mock_channel_old_write_tech = {
	.write = mock_channel_write,
	.write_video = mock_channel_write,
	.hangup = mock_channel_hangup,
};

AST_TEST_DEFINE(stream_write_non_multistream)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	struct ast_channel *mock_channel;
	struct mock_channel_pvt pvt = { 0, };
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct ast_frame frame = { 0, };

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_write_non_multistream";
		info->category = "/main/stream/";
		info->summary = "stream writing to non-multistream capable channel test";
		info->description =
			"Test that writing frames to a non-multistream channel works as expected";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ast_format_ulaw, 0)) {
		ast_test_status_update(test, "Failed to append a ulaw format to capabilities for channel nativeformats\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ast_format_h264, 0)) {
		ast_test_status_update(test, "Failed to append an h264 format to capabilities for channel nativeformats\n");
		return AST_TEST_FAIL;
	}

	mock_channel = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, "TestChannel");
	if (!mock_channel) {
		ast_test_status_update(test, "Failed to create a mock channel for testing\n");
		return AST_TEST_FAIL;
	}

	ast_channel_tech_set(mock_channel, &mock_channel_old_write_tech);
	ast_channel_nativeformats_set(mock_channel, caps);

	pvt.wrote = 0;
	ast_channel_tech_pvt_set(mock_channel, &pvt);
	ast_channel_unlock(mock_channel);

	frame.frametype = AST_FRAME_VOICE;
	frame.subclass.format = ast_format_ulaw;

	if (ast_write(mock_channel, &frame)) {
		ast_test_status_update(test, "Failed to write a ulaw frame to the mock channel when it should be fine\n");
		goto end;
	}

	if (!pvt.wrote) {
		ast_test_status_update(test, "Successfully wrote a frame of ulaw but it never reached the channel driver\n");
		goto end;
	}

	pvt.wrote = 0;

	if (!ast_write_stream(mock_channel, 2, &frame) || pvt.wrote) {
		ast_test_status_update(test, "Successfully wrote a frame of ulaw to a non-existent stream\n");
		goto end;
	}

	frame.frametype = AST_FRAME_VIDEO;
	frame.subclass.format = ast_format_h264;

	if (ast_write(mock_channel, &frame)) {
		ast_test_status_update(test, "Failed to write an h264 frame to the mock channel when it should be fine\n");
		goto end;
	}

	if (!pvt.wrote) {
		ast_test_status_update(test, "Successfully wrote a frame of h264 but it never reached the channel driver\n");
		goto end;
	}

	res = AST_TEST_PASS;

end:
	ast_hangup(mock_channel);

	return res;
}

static const struct ast_channel_tech mock_channel_write_stream_tech = {
	.write = mock_channel_write,
	.write_video = mock_channel_write,
	.write_stream = mock_channel_write_stream,
	.read_stream = mock_channel_read,
	.hangup = mock_channel_hangup,
};

AST_TEST_DEFINE(stream_write_multistream)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);
	struct ast_stream *stream;
	struct ast_channel *mock_channel;
	struct mock_channel_pvt pvt = { 0, };
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct ast_frame frame = { 0, };

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_write_multistream";
		info->category = "/main/stream/";
		info->summary = "stream writing to multistream capable channel test";
		info->description =
			"Test that writing frames to a multistream channel works as expected";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topology = ast_stream_topology_alloc();
	if (!topology) {
		ast_test_status_update(test, "Failed to create media stream topology\n");
		return AST_TEST_FAIL;
	}

	stream = ast_stream_alloc("audio", AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create an audio stream for testing multistream writing\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_free(stream);
		return AST_TEST_FAIL;
	}

	stream = ast_stream_alloc("audio2", AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create an audio stream for testing multistream writing\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_free(stream);
		return AST_TEST_FAIL;
	}

	stream = ast_stream_alloc("video", AST_MEDIA_TYPE_VIDEO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create a video stream for testing multistream writing\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_free(stream);
		return AST_TEST_FAIL;
	}

	stream = ast_stream_alloc("video2", AST_MEDIA_TYPE_VIDEO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create a video stream for testing multistream writing\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_free(stream);
		return AST_TEST_FAIL;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ast_format_ulaw, 0)) {
		ast_test_status_update(test, "Failed to append a ulaw format to capabilities for channel nativeformats\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ast_format_h264, 0)) {
		ast_test_status_update(test, "Failed to append an h264 format to capabilities for channel nativeformats\n");
		return AST_TEST_FAIL;
	}

	mock_channel = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, "TestChannel");
	if (!mock_channel) {
		ast_test_status_update(test, "Failed to create a mock channel for testing\n");
		return AST_TEST_FAIL;
	}

	ast_channel_tech_set(mock_channel, &mock_channel_write_stream_tech);
	ast_channel_set_stream_topology(mock_channel, topology);
	ast_channel_nativeformats_set(mock_channel, caps);
	topology = NULL;

	ast_channel_tech_pvt_set(mock_channel, &pvt);
	ast_channel_unlock(mock_channel);

	frame.frametype = AST_FRAME_VOICE;
	frame.subclass.format = ast_format_ulaw;
	pvt.stream_num = -1;

	if (ast_write(mock_channel, &frame)) {
		ast_test_status_update(test, "Failed to write a ulaw frame to the mock channel when it should be fine\n");
		goto end;
	}

	if (pvt.wrote) {
		ast_test_status_update(test, "Successfully wrote a frame of ulaw but it ended up on the old write callback instead of write_stream\n");
		goto end;
	}

	if (!pvt.wrote_stream) {
		ast_test_status_update(test, "Successfully wrote a frame of ulaw but it never reached the channel driver\n");
		goto end;
	}

	if (pvt.stream_num != 0) {
		ast_test_status_update(test, "Successfully wrote a frame of ulaw to the default stream but it ended up on stream %d and not 0\n",
			pvt.stream_num);
		goto end;
	}

	pvt.wrote_stream = 0;
	pvt.stream_num = -1;

	if (ast_write_stream(mock_channel, 0, &frame)) {
		ast_test_status_update(test, "Failed to write a ulaw frame to the first audio stream\n");
		goto end;
	}

	if (pvt.wrote) {
		ast_test_status_update(test, "Successfully wrote a frame of ulaw to the first audio stream but it ended up on the old write callback instead of write_stream\n");
		goto end;
	}

	if (!pvt.wrote_stream) {
		ast_test_status_update(test, "Successfully wrote a frame of ulaw to the first audio stream but it never reached the channel driver\n");
		goto end;
	}

	if (pvt.stream_num != 0) {
		ast_test_status_update(test, "Successfully wrote a frame of ulaw to the first audio stream but it ended up on stream %d and not 0\n",
			pvt.stream_num);
		goto end;
	}

	pvt.wrote_stream = 0;
	pvt.stream_num = -1;

	if (ast_write_stream(mock_channel, 1, &frame)) {
		ast_test_status_update(test, "Failed to write a ulaw frame to the second audio stream\n");
		goto end;
	}

	if (pvt.wrote) {
		ast_test_status_update(test, "Successfully wrote a frame of ulaw to the second audio stream but it ended up on the old write callback instead of write_stream\n");
		goto end;
	}

	if (!pvt.wrote_stream) {
		ast_test_status_update(test, "Successfully wrote a frame of ulaw to the second audio stream but it never reached the channel driver\n");
		goto end;
	}

	if (pvt.stream_num != 1) {
		ast_test_status_update(test, "Successfully wrote a frame of ulaw to the second audio stream but it ended up on stream %d and not 1\n",
			pvt.stream_num);
		goto end;
	}

	pvt.wrote_stream = 0;
	pvt.stream_num = -1;

	frame.frametype = AST_FRAME_VIDEO;
	frame.subclass.format = ast_format_h264;

	if (ast_write(mock_channel, &frame)) {
		ast_test_status_update(test, "Failed to write an h264 frame to the mock channel when it should be fine\n");
		goto end;
	}

	if (pvt.wrote) {
		ast_test_status_update(test, "Successfully wrote a frame of h264 but it ended up on the old write callback instead of write_stream\n");
		goto end;
	}

	if (!pvt.wrote_stream) {
		ast_test_status_update(test, "Successfully wrote a frame of h264 but it never reached the channel driver\n");
		goto end;
	}

	if (pvt.stream_num != 2) {
		ast_test_status_update(test, "Successfully wrote a frame of h264 to the default stream but it ended up on stream %d and not 2\n",
			pvt.stream_num);
		goto end;
	}

	pvt.wrote_stream = 0;
	pvt.stream_num = -1;

	if (ast_write_stream(mock_channel, 2, &frame)) {
		ast_test_status_update(test, "Failed to write an h264 frame to the first video stream\n");
		goto end;
	}

	if (pvt.wrote) {
		ast_test_status_update(test, "Successfully wrote a frame of h264 to the first video stream but it ended up on the old write callback instead of write_stream\n");
		goto end;
	}

	if (!pvt.wrote_stream) {
		ast_test_status_update(test, "Successfully wrote a frame of h264 to the first video stream but it never reached the channel driver\n");
		goto end;
	}

	if (pvt.stream_num != 2) {
		ast_test_status_update(test, "Successfully wrote a frame of h264 to the first video stream but it ended up on stream %d and not 2\n",
			pvt.stream_num);
		goto end;
	}

	pvt.wrote_stream = 0;
	pvt.stream_num = -1;

	if (ast_write_stream(mock_channel, 3, &frame)) {
		ast_test_status_update(test, "Failed to write an h264 frame to the second video stream\n");
		goto end;
	}

	if (pvt.wrote) {
		ast_test_status_update(test, "Successfully wrote a frame of h264 to the second video stream but it ended up on the old write callback instead of write_stream\n");
		goto end;
	}

	if (!pvt.wrote_stream) {
		ast_test_status_update(test, "Successfully wrote a frame of h264 to the second video stream but it never reached the channel driver\n");
		goto end;
	}

	if (pvt.stream_num != 3) {
		ast_test_status_update(test, "Successfully wrote a frame of h264 to the second video stream but it ended up on stream %d and not 3\n",
			pvt.stream_num);
		goto end;
	}

	pvt.wrote_stream = 0;
	pvt.stream_num = -1;

	if (!ast_write_stream(mock_channel, 9, &frame)) {
		ast_test_status_update(test, "Successfully wrote a frame of h264 to a non-existent stream\n");
		goto end;
	}

	if (pvt.wrote) {
		ast_test_status_update(test, "Successfully wrote a frame of h264 to a non-existent stream and it ended up on the old write callback\n");
		goto end;
	}

	if (pvt.wrote_stream) {
		ast_test_status_update(test, "Successfully wrote a frame of h264 to a non-existent stream and it ended up on the write_stream callback\n");
		goto end;
	}

	res = AST_TEST_PASS;

end:
	ast_hangup(mock_channel);

	return res;
}

static int load_stream_readqueue(struct ast_channel *chan, int frames)
{
	struct mock_channel_pvt *pvt = ast_channel_tech_pvt(chan);
	struct ast_frame f = { 0, };
	struct ast_frame *frame = NULL;
	int i;

	while ((frame = AST_LIST_REMOVE_HEAD(ast_channel_readq(chan), frame_list)))
			ast_frfree(frame);

	for (i = 0; i < frames; i++) {
		if (pvt->frame_count % 2 == 0) {
			f.frametype = AST_FRAME_VOICE;
			f.subclass.format = ast_format_ulaw;
		} else {
			f.frametype = AST_FRAME_VIDEO;
			f.subclass.format = ast_format_h264;
		}
		f.stream_num = pvt->frame_count % pvt->streams;
		f.seqno = pvt->frame_count;
		ast_queue_frame(chan, &f);
		pvt->frame_count++;
	}

	return 0;
}

static struct ast_channel *make_channel(struct ast_test *test, int streams,
		struct ast_channel_tech *tech)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	struct ast_channel *mock_channel = NULL;
	struct mock_channel_pvt *pvt = NULL;
	struct ast_stream_topology *topology = NULL;
	struct ast_stream *stream;
	enum ast_test_result_state res = AST_TEST_PASS;
	int i;

	mock_channel = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, "TestChannel");
	ast_test_validate_cleanup(test, mock_channel, res, done);
	ast_channel_tech_set(mock_channel, tech);

	if (tech->read_stream) {
		topology = ast_stream_topology_alloc();
		ast_test_validate_cleanup(test, topology, res, done);

		for (i = 0; i < streams; i++) {
			stream = ast_stream_alloc((i % 2 ? "video": "audio"), (i % 2 ? AST_MEDIA_TYPE_VIDEO : AST_MEDIA_TYPE_AUDIO));
			ast_test_validate_cleanup(test, stream, res, done);
			ast_test_validate_cleanup(test, ast_stream_topology_append_stream(topology, stream) == i, res, done);
		}
		ast_test_validate_cleanup(test, ast_stream_topology_get_count(topology) == streams, res, done);
		ast_channel_set_stream_topology(mock_channel, topology);
		topology = NULL;
	} else {
		caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		ast_test_validate_cleanup(test, caps, res, done);

		ast_test_validate_cleanup(test, ast_format_cap_append(caps, ast_format_ulaw, 0) == 0, res, done);
		ast_test_validate_cleanup(test, ast_format_cap_append(caps, ast_format_h264, 0) == 0, res, done);
		ast_channel_nativeformats_set(mock_channel, caps);
	}

	pvt = ast_calloc(1, sizeof(*pvt));
	ast_test_validate_cleanup(test, pvt, res, done);
	pvt->mallocd = 1;
	ast_channel_tech_pvt_set(mock_channel, pvt);

	ast_channel_unlock(mock_channel);

done:
	ast_stream_topology_free(topology);
	if (res == AST_TEST_FAIL && mock_channel) {
		ast_hangup(mock_channel);
	}

	return mock_channel;
}

enum CHANNEL_READ_TYPE {
	CHANNEL_READ,
	CHANNEL_READ_STREAM
};

static struct ast_frame *read_from_chan(enum CHANNEL_READ_TYPE rt, struct ast_channel *chan)
{
	if (rt == CHANNEL_READ_STREAM) {
		return ast_read_stream(chan);
	} else {
		return ast_read(chan);
	}
}

static enum ast_test_result_state read_test(struct ast_test *test, struct ast_channel_tech *tech,
		enum CHANNEL_READ_TYPE rt, int streams, int frames, int frames_per_read, int expected_nulls)
{
	struct ast_channel *mock_channel;
	struct mock_channel_pvt *pvt;
	struct ast_frame *fr = NULL;
	enum ast_test_result_state res = AST_TEST_PASS;
	int i = 0;
	int null_frames = 0;

	ast_test_status_update(test, "ChanType: %s ReadType: %s Streams: %d Frames: %d Frames per read: %d Expected Nulls: %d\n",
			tech->read_stream ? "MULTI" : "NON-MULTI",
			rt == CHANNEL_READ_STREAM ? "STREAM" : "NON-STREAM",
			streams, frames, frames_per_read, expected_nulls);
	mock_channel = make_channel(test, 4, tech);
	ast_test_validate_cleanup(test, mock_channel, res, done);

	pvt = ast_channel_tech_pvt(mock_channel);
	pvt->frame_count = 0;
	pvt->frame_limit = frames;
	pvt->streams = streams;
	pvt->frames_per_read = frames_per_read;

	load_stream_readqueue(mock_channel, frames / 2);
	ast_channel_fdno_set(mock_channel, 0);

	while ((fr = read_from_chan(rt, mock_channel))) {
		ast_channel_fdno_set(mock_channel, 0);
		if (fr->frametype != AST_FRAME_NULL) {
			ast_test_validate_cleanup(test, i == fr->seqno, res, done);
			ast_test_validate_cleanup(test, fr->frametype == ( i % 2 ? AST_FRAME_VIDEO : AST_FRAME_VOICE), res, done);
			ast_test_validate_cleanup(test, fr->stream_num == ( i % streams ), res, done);
			ast_frfree(fr);
		} else {
			null_frames++;
		}
		fr = NULL;
		i++;
	}
	ast_test_validate_cleanup(test, i == frames, res, done);
	ast_test_validate_cleanup(test, null_frames == expected_nulls, res, done);

done:
	ast_test_status_update(test, "    Frames read: %d NULL frames: %d\n", i, null_frames);
	ast_hangup(mock_channel);

	return res;
}

AST_TEST_DEFINE(stream_read_non_multistream)
{
	struct ast_channel_tech tech = {
		.read = mock_channel_read,
		.hangup = mock_channel_hangup,
	};

	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_read_non_multistream";
		info->category = "/main/stream/";
		info->summary = "stream reading from non-multistream capable channel test";
		info->description =
			"Test that reading frames from a non-multistream channel works as expected";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	res = read_test(test, &tech, CHANNEL_READ, 2, 16, 1, 0);
	ast_test_validate(test, res == AST_TEST_PASS, "non multi, non read stream, 2 stream");

	res = read_test(test, &tech, CHANNEL_READ_STREAM, 2, 16, 1, 0);
	ast_test_validate(test, res == AST_TEST_PASS, "non multi, read stream, 2 stream");

	res = read_test(test, &tech, CHANNEL_READ, 2, 16, 3, 0);
	ast_test_validate(test, res == AST_TEST_PASS, "non multi, non read stream, 2 stream, 3 frames per read");

	res = read_test(test, &tech, CHANNEL_READ_STREAM, 2, 16, 3, 0);
	ast_test_validate(test, res == AST_TEST_PASS, "non multi, read stream, 2 stream, 3 frames per read");

	return res;
}

AST_TEST_DEFINE(stream_read_multistream)
{
	struct ast_channel_tech tech = {
		.read_stream = mock_channel_read,
		.write_stream = mock_channel_write_stream,
		.hangup = mock_channel_hangup,
	};
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_read_multistream";
		info->category = "/main/stream/";
		info->summary = "stream reading from multistream capable channel test";
		info->description =
			"Test that reading frames from a multistream channel works as expected";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	res = read_test(test, &tech, CHANNEL_READ, 2, 16, 1, 0);
	ast_test_validate(test, res == AST_TEST_PASS, "multi, non read stream, 2 stream");

	res = read_test(test, &tech, CHANNEL_READ_STREAM, 2, 16, 1, 0);
	ast_test_validate(test, res == AST_TEST_PASS, "multi, read stream, 2 stream");

	res = read_test(test, &tech, CHANNEL_READ, 4, 16, 1, 8);
	ast_test_validate(test, res == AST_TEST_PASS, "multi, non read stream, 4 stream");

	res = read_test(test, &tech, CHANNEL_READ_STREAM, 4, 16, 1, 0);
	ast_test_validate(test, res == AST_TEST_PASS, "multi, read stream, 4 stream");

	res = read_test(test, &tech, CHANNEL_READ, 2, 16, 3, 0);
	ast_test_validate(test, res == AST_TEST_PASS, "multi, non read stream, 2 stream, 3 frames per read");

	res = read_test(test, &tech, CHANNEL_READ_STREAM, 2, 16, 3, 0);
	ast_test_validate(test, res == AST_TEST_PASS, "multi, read stream, 2 stream, 3 frames per read");

	res = read_test(test, &tech, CHANNEL_READ, 4, 16, 3, 8);
	ast_test_validate(test, res == AST_TEST_PASS, "multi, non read stream, 4 stream, 3 frames per read");

	res = read_test(test, &tech, CHANNEL_READ_STREAM, 4, 16, 3, 0);
	ast_test_validate(test, res == AST_TEST_PASS, "multi, read stream, 4 stream, 3 frames per read");

	return res;
}

AST_TEST_DEFINE(stream_topology_change_request_from_application_non_multistream)
{
	struct ast_channel_tech tech = {
		.read = mock_channel_read,
		.indicate = mock_channel_indicate,
		.hangup = mock_channel_hangup,
	};
	struct ast_channel *mock_channel;
	struct mock_channel_pvt *pvt;
	enum ast_test_result_state res = AST_TEST_PASS;
	int change_res;
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_change_request_from_application_non_multistream";
		info->category = "/main/stream/";
		info->summary = "stream topology changing on non-multistream channel test";
		info->description =
			"Test that an application trying to change the stream topology of a non-multistream channel gets a failure";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	mock_channel = make_channel(test, 1, &tech);
	ast_test_validate_cleanup(test, mock_channel, res, done);

	pvt = ast_channel_tech_pvt(mock_channel);
	pvt->indicated_change_request = 0;
	pvt->indicated_changed = 0;

	topology = ast_stream_topology_alloc();
	ast_test_validate_cleanup(test, topology, res, done);

	change_res = ast_channel_request_stream_topology_change(mock_channel, topology, NULL);

	ast_test_validate_cleanup(test, change_res == -1, res, done);
	ast_test_validate_cleanup(test, !pvt->indicated_change_request, res, done);

	ast_channel_lock(mock_channel);
	change_res = ast_channel_stream_topology_changed(mock_channel, topology);
	ast_channel_unlock(mock_channel);

	ast_test_validate_cleanup(test, change_res == -1, res, done);
	ast_test_validate_cleanup(test, !pvt->indicated_changed, res, done);

done:
	ast_hangup(mock_channel);

	return res;
}

AST_TEST_DEFINE(stream_topology_change_request_from_channel_non_multistream)
{
	struct ast_channel_tech tech = {
		.read_stream = mock_channel_read,
		.write_stream = mock_channel_write_stream,
		.indicate = mock_channel_indicate,
		.hangup = mock_channel_hangup,
	};
	struct ast_channel *mock_channel;
	struct mock_channel_pvt *pvt;
	enum ast_test_result_state res = AST_TEST_PASS;
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);
	struct ast_frame request_change = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = AST_CONTROL_STREAM_TOPOLOGY_REQUEST_CHANGE,
	};
	struct ast_frame *fr = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_change_request_from_channel_non_multistream";
		info->category = "/main/stream/";
		info->summary = "channel requesting stream topology change to non-multistream application test";
		info->description =
			"Test that a channel requesting a stream topology change from a non-multistream application does not work";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	mock_channel = make_channel(test, 1, &tech);
	ast_test_validate_cleanup(test, mock_channel, res, done);

	pvt = ast_channel_tech_pvt(mock_channel);
	pvt->indicated_changed = 0;

	topology = ast_stream_topology_alloc();
	ast_test_validate_cleanup(test, topology, res, done);

	request_change.data.ptr = topology;
	ast_queue_frame(mock_channel, &request_change);

	fr = ast_read(mock_channel);
	ast_test_validate_cleanup(test, fr, res, done);
	ast_test_validate_cleanup(test, fr == &ast_null_frame, res, done);
	ast_test_validate_cleanup(test, pvt->indicated_changed, res, done);

done:
	if (fr) {
		ast_frfree(fr);
	}
	ast_hangup(mock_channel);

	return res;
}

AST_TEST_DEFINE(stream_topology_change_request_from_application)
{
	struct ast_channel_tech tech = {
		.read_stream = mock_channel_read,
		.write_stream = mock_channel_write_stream,
		.indicate = mock_channel_indicate,
		.hangup = mock_channel_hangup,
	};
	struct ast_channel *mock_channel;
	struct mock_channel_pvt *pvt;
	enum ast_test_result_state res = AST_TEST_PASS;
	int change_res;
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_change_request_from_application";
		info->category = "/main/stream/";
		info->summary = "stream topology change request from application test";
		info->description =
			"Test that an application changing the stream topology of a multistream capable channel receives success";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	mock_channel = make_channel(test, 1, &tech);
	ast_test_validate_cleanup(test, mock_channel, res, done);

	pvt = ast_channel_tech_pvt(mock_channel);
	pvt->indicated_change_request = 0;
	pvt->indicated_changed = 0;

	topology = ast_stream_topology_alloc();
	ast_test_validate_cleanup(test, topology, res, done);

	change_res = ast_channel_request_stream_topology_change(mock_channel, topology, NULL);

	ast_test_validate_cleanup(test, !change_res, res, done);
	ast_test_validate_cleanup(test, pvt->indicated_change_request, res, done);

	ast_channel_lock(mock_channel);
	change_res = ast_channel_stream_topology_changed(mock_channel, topology);
	ast_channel_unlock(mock_channel);

	ast_test_validate_cleanup(test, !change_res, res, done);
	ast_test_validate_cleanup(test, pvt->indicated_changed, res, done);

done:
	ast_hangup(mock_channel);

	return res;
}

AST_TEST_DEFINE(stream_topology_change_request_from_channel)
{
	struct ast_channel_tech tech = {
		.read_stream = mock_channel_read,
		.write_stream = mock_channel_write_stream,
		.indicate = mock_channel_indicate,
		.hangup = mock_channel_hangup,
	};
	struct ast_channel *mock_channel;
	struct mock_channel_pvt *pvt;
	enum ast_test_result_state res = AST_TEST_PASS;
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_free);
	struct ast_frame request_change = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = AST_CONTROL_STREAM_TOPOLOGY_REQUEST_CHANGE,
	};
	struct ast_frame *fr = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_change_request_from_channel";
		info->category = "/main/stream/";
		info->summary = "channel requesting stream topology change to multistream application test";
		info->description =
			"Test that a channel requesting a stream topology change from a multistream application works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	mock_channel = make_channel(test, 1, &tech);
	ast_test_validate_cleanup(test, mock_channel, res, done);

	pvt = ast_channel_tech_pvt(mock_channel);
	pvt->indicated_changed = 0;

	topology = ast_stream_topology_alloc();
	ast_test_validate_cleanup(test, topology, res, done);

	request_change.data.ptr = topology;
	ast_queue_frame(mock_channel, &request_change);

	fr = ast_read_stream(mock_channel);
	ast_test_validate_cleanup(test, fr, res, done);
	ast_test_validate_cleanup(test, fr->frametype == AST_FRAME_CONTROL, res, done);
	ast_test_validate_cleanup(test, fr->subclass.integer == AST_CONTROL_STREAM_TOPOLOGY_REQUEST_CHANGE, res, done);
	ast_test_validate_cleanup(test, !pvt->indicated_changed, res, done);

done:
	if (fr) {
		ast_frfree(fr);
	}
	ast_hangup(mock_channel);

	return res;
}

AST_TEST_DEFINE(format_cap_from_stream_topology)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, stream_caps, NULL, ao2_cleanup);
	struct ast_stream_topology *topology;
	struct ast_stream *stream;
	struct ast_format_cap *new_cap;

	switch (cmd) {
	case TEST_INIT:
		info->name = "format_cap_from_stream_topology";
		info->category = "/main/stream/";
		info->summary = "stream topology to format capabilities conversion test";
		info->description =
			"Test that converting a stream topology to format capabilities results in expected formats";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ast_format_ulaw, 0)) {
		ast_test_status_update(test, "Failed to append ulaw format to capabilities\n");
		return AST_TEST_FAIL;
	}

	if (ast_format_cap_append(caps, ast_format_h264, 0)) {
		ast_test_status_update(test, "Failed to append h264 format to capabilities\n");
		return AST_TEST_FAIL;
	}

	topology = ast_stream_topology_create_from_format_cap(caps);
	if (!topology) {
		ast_test_status_update(test, "Failed to create a stream topology from format capabilities of ulaw and h264\n");
		return AST_TEST_FAIL;
	}

	/*
	 * Append declined stream with formats that should not be included
	 * in combined topology caps.
	 */
	stream = ast_stream_alloc("audio", AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create an audio stream for testing stream topology\n");
		ast_stream_topology_free(topology);
		return AST_TEST_FAIL;
	}
	ast_stream_set_state(stream, AST_STREAM_STATE_REMOVED);
	new_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!new_cap) {
		ast_test_status_update(test, "Could not allocate an empty format capabilities structure\n");
		ast_stream_free(stream);
		ast_stream_topology_free(topology);
		return AST_TEST_FAIL;
	}
	if (ast_format_cap_append(new_cap, ast_format_alaw, 0)) {
		ast_test_status_update(test, "Failed to append alaw format to capabilities\n");
		ao2_cleanup(new_cap);
		ast_stream_free(stream);
		ast_stream_topology_free(topology);
		return AST_TEST_FAIL;
	}
	ast_stream_set_formats(stream, new_cap);
	ao2_cleanup(new_cap);
	if (ast_stream_topology_append_stream(topology, stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_free(stream);
		ast_stream_topology_free(topology);
		return AST_TEST_FAIL;
	}

	stream_caps = ast_stream_topology_get_formats(topology);
	if (!stream_caps) {
		ast_test_status_update(test, "Failed to create a format capabilities from a stream topology\n");
		ast_stream_topology_free(topology);
		return AST_TEST_FAIL;
	}

	ast_stream_topology_free(topology);

	if (!ast_format_cap_identical(caps, stream_caps)) {
		ast_test_status_update(test, "Converting format capabilities into topology and back resulted in different formats\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

#define topology_append_stream(topology, name, type, res, label) \
	do { \
		struct ast_stream *__stream = ast_stream_alloc((name), (type)); \
		ast_test_validate_cleanup(test, __stream, res, label); \
		if (ast_stream_topology_append_stream((topology), __stream) < 0) { \
			ast_stream_free(__stream); \
			res = AST_TEST_FAIL; \
			goto label;	     \
		} \
	} while(0)

AST_TEST_DEFINE(stream_topology_map_create)
{
	RAII_VAR(struct ast_stream_topology *, t0, NULL, ast_stream_topology_free);

	struct ast_vector_int types = { NULL };
	struct ast_vector_int v0 = { NULL };
	struct ast_vector_int v1 = { NULL };

	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "stream_topology_map_create";
		info->category = "/main/stream/";
		info->summary = "stream topology map creation unit test";
		info->description =
			"Test that creating a stream topology map works";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, AST_VECTOR_INIT(&types, 5) == 0);

	/* Map a first topology and check that it mapped one to one */
	ast_test_validate_cleanup(test, (t0 = ast_stream_topology_alloc()), res, done);
	topology_append_stream(t0, "audio", AST_MEDIA_TYPE_AUDIO, res, done);
	topology_append_stream(t0, "video", AST_MEDIA_TYPE_VIDEO, res, done);

	ast_stream_topology_map(t0, &types, &v0, &v1);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&types) == 2, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&types, 0) == AST_MEDIA_TYPE_AUDIO, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&types, 1) == AST_MEDIA_TYPE_VIDEO, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v0, 0) == 0, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v0, 1) == 1, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v1, 0) == 0, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v1, 1) == 1, res, done);

	/* Map a second topology and check that it merged */
	ast_stream_topology_free(t0);
	ast_test_validate_cleanup(test, (t0 = ast_stream_topology_alloc()), res, done);
	topology_append_stream(t0, "video", AST_MEDIA_TYPE_VIDEO, res, done);
	topology_append_stream(t0, "audio", AST_MEDIA_TYPE_AUDIO, res, done);

	ast_stream_topology_map(t0, &types, &v0, &v1);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&types) == 2, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&types, 0) == AST_MEDIA_TYPE_AUDIO, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&types, 1) == AST_MEDIA_TYPE_VIDEO, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v0, 0) == 1, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v0, 1) == 0, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v1, 0) == 1, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v1, 1) == 0, res, done);

	/* Map a third topology with more streams and check that it merged */
	ast_stream_topology_free(t0);
	ast_test_validate_cleanup(test, (t0 = ast_stream_topology_alloc()), res, done);
	topology_append_stream(t0, "video", AST_MEDIA_TYPE_VIDEO, res, done);
	topology_append_stream(t0, "audio", AST_MEDIA_TYPE_AUDIO, res, done);
	topology_append_stream(t0, "audio", AST_MEDIA_TYPE_AUDIO, res, done);

	ast_stream_topology_map(t0, &types, &v0, &v1);
	ast_test_validate_cleanup(test, AST_VECTOR_SIZE(&types) == 3, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&types, 0) == AST_MEDIA_TYPE_AUDIO, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&types, 1) == AST_MEDIA_TYPE_VIDEO, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&types, 2) == AST_MEDIA_TYPE_AUDIO, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v0, 0) == 1, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v0, 1) == 0, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v0, 2) == 2, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v1, 0) == 1, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v1, 1) == 0, res, done);
	ast_test_validate_cleanup(test, AST_VECTOR_GET(&v1, 2) == 2, res, done);

done:
	AST_VECTOR_FREE(&v1);
	AST_VECTOR_FREE(&v0);
	AST_VECTOR_FREE(&types);

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(stream_create);
	AST_TEST_UNREGISTER(stream_create_no_name);
	AST_TEST_UNREGISTER(stream_set_type);
	AST_TEST_UNREGISTER(stream_set_formats);
	AST_TEST_UNREGISTER(stream_set_state);
	AST_TEST_UNREGISTER(stream_metadata);
	AST_TEST_UNREGISTER(stream_topology_create);
	AST_TEST_UNREGISTER(stream_topology_clone);
	AST_TEST_UNREGISTER(stream_topology_clone);
	AST_TEST_UNREGISTER(stream_topology_append_stream);
	AST_TEST_UNREGISTER(stream_topology_set_stream);
	AST_TEST_UNREGISTER(stream_topology_del_stream);
	AST_TEST_UNREGISTER(stream_topology_create_from_format_cap);
	AST_TEST_UNREGISTER(stream_topology_get_first_stream_by_type);
	AST_TEST_UNREGISTER(stream_topology_create_from_channel_nativeformats);
	AST_TEST_UNREGISTER(stream_topology_channel_set);
	AST_TEST_UNREGISTER(stream_write_non_multistream);
	AST_TEST_UNREGISTER(stream_write_multistream);
	AST_TEST_UNREGISTER(stream_read_non_multistream);
	AST_TEST_UNREGISTER(stream_read_multistream);
	AST_TEST_UNREGISTER(stream_topology_change_request_from_application_non_multistream);
	AST_TEST_UNREGISTER(stream_topology_change_request_from_channel_non_multistream);
	AST_TEST_UNREGISTER(stream_topology_change_request_from_application);
	AST_TEST_UNREGISTER(stream_topology_change_request_from_channel);
	AST_TEST_UNREGISTER(format_cap_from_stream_topology);
	AST_TEST_UNREGISTER(stream_topology_map_create);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(stream_create);
	AST_TEST_REGISTER(stream_create_no_name);
	AST_TEST_REGISTER(stream_set_type);
	AST_TEST_REGISTER(stream_set_formats);
	AST_TEST_REGISTER(stream_set_state);
	AST_TEST_REGISTER(stream_metadata);
	AST_TEST_REGISTER(stream_topology_create);
	AST_TEST_REGISTER(stream_topology_clone);
	AST_TEST_REGISTER(stream_topology_append_stream);
	AST_TEST_REGISTER(stream_topology_set_stream);
	AST_TEST_REGISTER(stream_topology_del_stream);
	AST_TEST_REGISTER(stream_topology_create_from_format_cap);
	AST_TEST_REGISTER(stream_topology_get_first_stream_by_type);
	AST_TEST_REGISTER(stream_topology_create_from_channel_nativeformats);
	AST_TEST_REGISTER(stream_topology_channel_set);
	AST_TEST_REGISTER(stream_write_non_multistream);
	AST_TEST_REGISTER(stream_write_multistream);
	AST_TEST_REGISTER(stream_read_non_multistream);
	AST_TEST_REGISTER(stream_read_multistream);
	AST_TEST_REGISTER(stream_topology_change_request_from_application_non_multistream);
	AST_TEST_REGISTER(stream_topology_change_request_from_channel_non_multistream);
	AST_TEST_REGISTER(stream_topology_change_request_from_application);
	AST_TEST_REGISTER(stream_topology_change_request_from_channel);
	AST_TEST_REGISTER(format_cap_from_stream_topology);
	AST_TEST_REGISTER(stream_topology_map_create);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Media Stream API test module");
