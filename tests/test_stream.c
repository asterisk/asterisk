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

	if (ast_stream_get_type(ast_stream_topology_get_stream(cloned, 1)) != ast_stream_get_type(ast_stream_topology_get_stream(topology, 1))) {
		ast_test_status_update(test, "Cloned video stream does not contain same type as original\n");
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
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Media Stream API test module");
