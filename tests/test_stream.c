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

AST_TEST_DEFINE(stream_create)
{
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_destroy);

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

	stream = ast_stream_create("test", AST_MEDIA_TYPE_AUDIO);
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
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_destroy);

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

	stream = ast_stream_create(NULL, AST_MEDIA_TYPE_AUDIO);
	if (!stream) {
		ast_test_status_update(test, "Failed to create media stream given proper arguments\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_set_type)
{
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_destroy);

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

	stream = ast_stream_create("test", AST_MEDIA_TYPE_AUDIO);
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
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_destroy);
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

	stream = ast_stream_create("test", AST_MEDIA_TYPE_AUDIO);
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
	RAII_VAR(struct ast_stream *, stream, NULL, ast_stream_destroy);

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

	stream = ast_stream_create("test", AST_MEDIA_TYPE_AUDIO);
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

AST_TEST_DEFINE(stream_topology_create)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_destroy);

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

	topology = ast_stream_topology_create();
	if (!topology) {
		ast_test_status_update(test, "Failed to create media stream topology\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_topology_clone)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_destroy);
	RAII_VAR(struct ast_stream_topology *, cloned, NULL, ast_stream_topology_destroy);
	struct ast_stream *audio_stream, *video_stream;

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

	topology = ast_stream_topology_create();
	if (!topology) {
		ast_test_status_update(test, "Failed to create media stream topology\n");
		return AST_TEST_FAIL;
	}

	audio_stream = ast_stream_create("audio", AST_MEDIA_TYPE_AUDIO);
	if (!audio_stream) {
		ast_test_status_update(test, "Failed to create an audio stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, audio_stream) == -1) {
		ast_test_status_update(test, "Failed to append valid audio stream to stream topology\n");
		ast_stream_destroy(audio_stream);
		return AST_TEST_FAIL;
	}

	video_stream = ast_stream_create("video", AST_MEDIA_TYPE_VIDEO);
	if (!video_stream) {
		ast_test_status_update(test, "Failed to create a video stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, video_stream) == -1) {
		ast_test_status_update(test, "Failed to append valid video stream to stream topology\n");
		ast_stream_destroy(video_stream);
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

	if (ast_stream_get_type(ast_stream_topology_get_stream(cloned, 1)) != ast_stream_get_type(ast_stream_topology_get_stream(topology, 1))) {
		ast_test_status_update(test, "Cloned video stream does not contain same type as original\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(stream_topology_append_stream)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_destroy);
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

	topology = ast_stream_topology_create();
	if (!topology) {
		ast_test_status_update(test, "Failed to create media stream topology\n");
		return AST_TEST_FAIL;
	}

	audio_stream = ast_stream_create("audio", AST_MEDIA_TYPE_AUDIO);
	if (!audio_stream) {
		ast_test_status_update(test, "Failed to create an audio stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	position = ast_stream_topology_append_stream(topology, audio_stream);
	if (position == -1) {
		ast_test_status_update(test, "Failed to append valid audio stream to stream topology\n");
		ast_stream_destroy(audio_stream);
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

	video_stream = ast_stream_create("video", AST_MEDIA_TYPE_VIDEO);
	if (!video_stream) {
		ast_test_status_update(test, "Failed to create a video stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	position = ast_stream_topology_append_stream(topology, video_stream);
	if (position == -1) {
		ast_test_status_update(test, "Failed to append valid video stream to stream topology\n");
		ast_stream_destroy(video_stream);
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
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_destroy);
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

	topology = ast_stream_topology_create();
	if (!topology) {
		ast_test_status_update(test, "Failed to create media stream topology\n");
		return AST_TEST_FAIL;
	}

	audio_stream = ast_stream_create("audio", AST_MEDIA_TYPE_AUDIO);
	if (!audio_stream) {
		ast_test_status_update(test, "Failed to create an audio stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_set_stream(topology, 0, audio_stream)) {
		ast_test_status_update(test, "Failed to set an audio stream to a position where it is permitted\n");
		ast_stream_destroy(audio_stream);
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

	video_stream = ast_stream_create("video", AST_MEDIA_TYPE_VIDEO);
	if (!video_stream) {
		ast_test_status_update(test, "Failed to create a video stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_set_stream(topology, 0, video_stream)) {
		ast_test_status_update(test, "Failed to set a video stream to a position where it is permitted\n");
		ast_stream_destroy(video_stream);
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

	audio_stream = ast_stream_create("audio", AST_MEDIA_TYPE_AUDIO);
	if (!audio_stream) {
		ast_test_status_update(test, "Failed to create an audio stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_set_stream(topology, 1, audio_stream)) {
		ast_test_status_update(test, "Failed to set an audio stream to a position where it is permitted\n");
		ast_stream_destroy(audio_stream);
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

AST_TEST_DEFINE(stream_topology_create_from_format_cap)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_destroy);
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

	ast_stream_topology_destroy(topology);
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
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_destroy);
	struct ast_stream *first_stream, *second_stream, *third_stream, *fourth_stream;

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

	topology = ast_stream_topology_create();
	if (!topology) {
		ast_test_status_update(test, "Failed to create media stream topology\n");
		return AST_TEST_FAIL;
	}

	first_stream = ast_stream_create("audio", AST_MEDIA_TYPE_AUDIO);
	if (!first_stream) {
		ast_test_status_update(test, "Failed to create an audio stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, first_stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_destroy(first_stream);
		return AST_TEST_FAIL;
	}

	second_stream = ast_stream_create("audio2", AST_MEDIA_TYPE_AUDIO);
	if (!second_stream) {
		ast_test_status_update(test, "Failed to create a second audio stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, second_stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_destroy(second_stream);
		return AST_TEST_FAIL;
	}

	third_stream = ast_stream_create("video", AST_MEDIA_TYPE_VIDEO);
	if (!third_stream) {
		ast_test_status_update(test, "Failed to create a video stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, third_stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_destroy(third_stream);
		return AST_TEST_FAIL;
	}

	fourth_stream = ast_stream_create("video2", AST_MEDIA_TYPE_VIDEO);
	if (!fourth_stream) {
		ast_test_status_update(test, "Failed to create a second video stream for testing stream topology\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_append_stream(topology, fourth_stream) == -1) {
		ast_test_status_update(test, "Failed to append a perfectly good stream to a topology\n");
		ast_stream_destroy(fourth_stream);
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_first_stream_by_type(topology, AST_MEDIA_TYPE_AUDIO) != first_stream) {
		ast_test_status_update(test, "Retrieved first audio stream from topology but it is not the correct one\n");
		return AST_TEST_FAIL;
	}

	if (ast_stream_topology_get_first_stream_by_type(topology, AST_MEDIA_TYPE_VIDEO) != third_stream) {
		ast_test_status_update(test, "Retrieved first video stream from topology but it is not the correct one\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static const struct ast_channel_tech mock_channel_tech = {
};

AST_TEST_DEFINE(stream_topology_create_from_channel_nativeformats)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_destroy);
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	struct ast_channel *mock_channel;
	enum ast_test_result_state res = AST_TEST_PASS;

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
		res = AST_TEST_FAIL;
	} else if (ast_stream_topology_get_count(ast_channel_get_stream_topology(mock_channel)) != 2) {
		ast_test_status_update(test, "Set nativeformats on a channel to ulaw, alaw, and h264 and received '%d' streams instead of expected 2\n",
			ast_stream_topology_get_count(ast_channel_get_stream_topology(mock_channel)));
		res = AST_TEST_FAIL;
	}

	ast_channel_unlock(mock_channel);
	ast_hangup(mock_channel);

	return res;
}

static const struct ast_channel_tech mock_stream_channel_tech = {
	.properties = AST_CHAN_TP_MULTISTREAM,
};

AST_TEST_DEFINE(stream_topology_channel_set)
{
	RAII_VAR(struct ast_stream_topology *, topology, NULL, ast_stream_topology_destroy);
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

	topology = ast_stream_topology_create();
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

static int unload_module(void)
{
	AST_TEST_UNREGISTER(stream_create);
	AST_TEST_UNREGISTER(stream_create_no_name);
	AST_TEST_UNREGISTER(stream_set_type);
	AST_TEST_UNREGISTER(stream_set_formats);
	AST_TEST_UNREGISTER(stream_set_state);
	AST_TEST_UNREGISTER(stream_topology_create);
	AST_TEST_UNREGISTER(stream_topology_clone);
	AST_TEST_UNREGISTER(stream_topology_clone);
	AST_TEST_UNREGISTER(stream_topology_append_stream);
	AST_TEST_UNREGISTER(stream_topology_set_stream);
	AST_TEST_UNREGISTER(stream_topology_create_from_format_cap);
	AST_TEST_UNREGISTER(stream_topology_get_first_stream_by_type);
	AST_TEST_UNREGISTER(stream_topology_create_from_channel_nativeformats);
	AST_TEST_UNREGISTER(stream_topology_channel_set);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(stream_create);
	AST_TEST_REGISTER(stream_create_no_name);
	AST_TEST_REGISTER(stream_set_type);
	AST_TEST_REGISTER(stream_set_formats);
	AST_TEST_REGISTER(stream_set_state);
	AST_TEST_REGISTER(stream_topology_create);
	AST_TEST_REGISTER(stream_topology_clone);
	AST_TEST_REGISTER(stream_topology_append_stream);
	AST_TEST_REGISTER(stream_topology_set_stream);
	AST_TEST_REGISTER(stream_topology_create_from_format_cap);
	AST_TEST_REGISTER(stream_topology_get_first_stream_by_type);
	AST_TEST_REGISTER(stream_topology_create_from_channel_nativeformats);
	AST_TEST_REGISTER(stream_topology_channel_set);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Media Stream API test module");
