/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium Inc.
 *
 * Mark Michelson <mmmichelson@digium.com>
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

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/sdp.h"
#include "asterisk/stream.h"
#include "asterisk/format.h"
#include "asterisk/format_cache.h"
#include "asterisk/format_cap.h"

static int validate_o_line(struct ast_test *test, const struct ast_sdp_o_line *o_line,
	const char *sdpowner, const char *address_type, const char *address)
{
	if (!o_line) {
		return -1;
	}

	if (strcmp(o_line->username, sdpowner)) {
		ast_test_status_update(test, "Expected o-line SDP owner %s but got %s\n",
			sdpowner, o_line->username);
		return -1;
	}

	if (strcmp(o_line->address_type, address_type)) {
		ast_test_status_update(test, "Expected o-line SDP address type %s but got %s\n",
			address_type, o_line->address_type);
		return -1;
	}

	if (strcmp(o_line->address, address)) {
		ast_test_status_update(test, "Expected o-line SDP address %s but got %s\n",
			address, o_line->address);
		return -1;
	}

	ast_test_status_update(test, "SDP o-line is as expected!\n");
	return 0;
}

static int validate_c_line(struct ast_test *test, const struct ast_sdp_c_line *c_line,
	const char *address_type, const char *address)
{
	if (strcmp(c_line->address_type, address_type)) {
		ast_test_status_update(test, "Expected c-line SDP address type %s but got %s\n",
			address_type, c_line->address_type);
		return -1;
	}

	if (strcmp(c_line->address, address)) {
		ast_test_status_update(test, "Expected c-line SDP address %s but got %s\n",
			address, c_line->address);
		return -1;
	}

	ast_test_status_update(test, "SDP c-line is as expected!\n");
	return 0;
}

static int validate_m_line(struct ast_test *test, const struct ast_sdp_m_line *m_line,
	const char *media_type, int num_payloads)
{
	if (strcmp(m_line->type, media_type)) {
		ast_test_status_update(test, "Expected m-line media type %s but got %s\n",
			media_type, m_line->type);
		return -1;
	}

	if (ast_sdp_m_get_payload_count(m_line) != num_payloads) {
		ast_test_status_update(test, "Expected m-line payload count %d but got %d\n",
			num_payloads, ast_sdp_m_get_payload_count(m_line));
		return -1;
	}

	ast_test_status_update(test, "SDP m-line is as expected\n");
	return 0;
}

static int validate_rtpmap(struct ast_test *test, const struct ast_sdp_m_line *m_line,
	const char *media_name)
{
	struct ast_sdp_a_line *a_line;
	int i;

	for (i = 0; i < ast_sdp_m_get_a_count(m_line); ++i) {
		struct ast_sdp_rtpmap *rtpmap;
		int match;

		a_line = ast_sdp_m_get_a(m_line, i);
		if (strcmp(a_line->name, "rtpmap")) {
			continue;
		}

		rtpmap = ast_sdp_a_get_rtpmap(a_line);
		if (!rtpmap) {
			return -1;
		}

		match = !strcmp(rtpmap->encoding_name, media_name);

		ast_sdp_rtpmap_free(rtpmap);
		if (match) {
			return 0;
		}
	}

	ast_test_status_update(test, "Could not find rtpmap with encoding name %s\n", media_name);

	return -1;
}

AST_TEST_DEFINE(invalid_rtpmap)
{
	/* a=rtpmap: is already assumed. This is the part after that */
	static const char *invalids[] = {
		"J PCMU/8000",
		"0 PCMU:8000",
		"0 PCMU/EIGHT-THOUSAND",
		"0 PCMU/8000million/2",
		"0 PCMU//2",
		"0 /8000/2",
		"0 PCMU/8000/",
		"0 PCMU/8000million",
	};
	int i;
	enum ast_test_result_state res = AST_TEST_PASS;

	switch(cmd) {
	case TEST_INIT:
		info->name = "invalid_rtpmap";
		info->category = "/main/sdp/";
		info->summary = "Ensure invalid rtpmaps are rejected";
		info->description =
			"Try to convert several invalid rtpmap attributes. If\n"
			"any succeeds, the test fails.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(invalids); ++i) {
		struct ast_sdp_a_line *a_line;
		struct ast_sdp_rtpmap *rtpmap;

		a_line = ast_sdp_a_alloc("rtpmap", invalids[i]);
		rtpmap = ast_sdp_a_get_rtpmap(a_line);
		if (rtpmap) {
			ast_test_status_update(test, "Invalid rtpmap '%s' was accepted as valid\n",
				invalids[i]);
			res = AST_TEST_FAIL;
		}
		ast_sdp_a_free(a_line);
		ast_sdp_rtpmap_free(rtpmap);
	}

	return res;
}

AST_TEST_DEFINE(rtpmap)
{
	static const char *valids[] = {
		"0 PCMU/8000",
		"107 opus/48000/2",
	};
	static int payloads[] = {
		0,
		107,
	};
	static const char *encoding_names[] = {
		"PCMU",
		"opus",
	};
	static int clock_rates[] = {
		8000,
		48000,
	};
	static const char *encoding_parameters[] = {
		"",
		"2",
	};
	int i;
	enum ast_test_result_state res = AST_TEST_PASS;

	switch(cmd) {
	case TEST_INIT:
		info->name = "rtpmap";
		info->category = "/main/sdp/";
		info->summary = "Ensure rtpmap attribute values are parsed correctly";
		info->description =
			"Parse several valid rtpmap attributes. Ensure that the parsed values\n"
			"are what we expect";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(valids); ++i) {
		struct ast_sdp_a_line *a_line;
		struct ast_sdp_rtpmap *rtpmap;

		a_line = ast_sdp_a_alloc("rtpmap", valids[i]);
		rtpmap = ast_sdp_a_get_rtpmap(a_line);
		if (!rtpmap) {
			ast_test_status_update(test, "Valid rtpmap '%s' was rejected as invalid\n",
				valids[i]);
			res = AST_TEST_FAIL;
			continue;
		}
		if (rtpmap->payload != payloads[i]) {
			ast_test_status_update(test, "RTPmap payload '%d' does not match expected '%d'\n",
				rtpmap->payload, payloads[i]);
			res = AST_TEST_FAIL;
		}
		if (strcmp(rtpmap->encoding_name, encoding_names[i])) {
			ast_test_status_update(test, "RTPmap encoding_name '%s' does not match expected '%s'\n",
				rtpmap->encoding_name, encoding_names[i]);
			res = AST_TEST_FAIL;
		}
		if (rtpmap->clock_rate != clock_rates[i]) {
			ast_test_status_update(test, "RTPmap clock rate '%d' does not match expected '%d'\n",
				rtpmap->clock_rate, clock_rates[i]);
			res = AST_TEST_FAIL;
		}
		if (strcmp(rtpmap->encoding_parameters, encoding_parameters[i])) {
			ast_test_status_update(test, "RTPmap encoding_parameter '%s' does not match expected '%s'\n",
				rtpmap->encoding_parameters, encoding_parameters[i]);
			res = AST_TEST_FAIL;
		}
		ast_sdp_a_free(a_line);
		ast_sdp_rtpmap_free(rtpmap);
	}

	return res;
}

AST_TEST_DEFINE(find_attr)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_sdp_m_line *m_line;
	struct ast_sdp_a_line *a_line;

	switch(cmd) {
	case TEST_INIT:
		info->name = "find_attr";
		info->category = "/main/sdp/";
		info->summary = "Ensure that finding attributes works as expected";
		info->description =
			"An SDP m-line is created, and two attributes are added.\n"
			"We then attempt a series of attribute-finding calls that are expected to work\n"
			"followed by a series of attribute-finding calls that are expected fo fail.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	m_line = ast_sdp_m_alloc("audio", 666, 1, "RTP/AVP", NULL);
	if (!m_line) {
		res = AST_TEST_FAIL;
		goto end;
	}
	a_line = ast_sdp_a_alloc("foo", "0 bar");
	if (!a_line) {
		res = AST_TEST_FAIL;
		goto end;
	}
	ast_sdp_m_add_a(m_line, a_line);

	a_line = ast_sdp_a_alloc("baz", "howdy");
	if (!a_line) {
		res = AST_TEST_FAIL;
		goto end;
	}
	ast_sdp_m_add_a(m_line, a_line);

	/* These should work */
	a_line = ast_sdp_m_find_attribute(m_line, "foo", 0);
	if (!a_line) {
		ast_test_status_update(test, "Failed to find attribute 'foo' with payload '0'\n");
		res = AST_TEST_FAIL;
	}
	a_line = ast_sdp_m_find_attribute(m_line, "foo", -1);
	if (!a_line) {
		ast_test_status_update(test, "Failed to find attribute 'foo' with unspecified payload\n");
		res = AST_TEST_FAIL;
	}
	a_line = ast_sdp_m_find_attribute(m_line, "baz", -1);
	if (!a_line) {
		ast_test_status_update(test, "Failed to find attribute 'baz' with unspecified payload\n");
		res = AST_TEST_FAIL;
	}

	/* These should fail */
	a_line = ast_sdp_m_find_attribute(m_line, "foo", 1);
	if (a_line) {
		ast_test_status_update(test, "Found non-existent attribute 'foo' with payload '1'\n");
		res = AST_TEST_FAIL;
	}
	a_line = ast_sdp_m_find_attribute(m_line, "baz", 0);
	if (a_line) {
		ast_test_status_update(test, "Found non-existent attribute 'baz' with payload '0'\n");
		res = AST_TEST_FAIL;
	}
	a_line = ast_sdp_m_find_attribute(m_line, "wibble", 0);
	if (a_line) {
		ast_test_status_update(test, "Found non-existent attribute 'wibble' with payload '0'\n");
		res = AST_TEST_FAIL;
	}
	a_line = ast_sdp_m_find_attribute(m_line, "wibble", -1);
	if (a_line) {
		ast_test_status_update(test, "Found non-existent attribute 'foo' with unspecified payload\n");
		res = AST_TEST_FAIL;
	}

end:
	ast_sdp_m_free(m_line);
	return res;
}

struct sdp_format {
	enum ast_media_type type;
	const char *formats;
};

static struct ast_sdp_state *build_sdp_state(int num_streams, const struct sdp_format *formats)
{
	struct ast_stream_topology *topology = NULL;
	struct ast_sdp_state *state = NULL;
	struct ast_sdp_options *options;
	int i;

	options = ast_sdp_options_alloc();
	if (!options) {
		goto end;
	}
	ast_sdp_options_set_media_address(options, "127.0.0.1");
	ast_sdp_options_set_sdpowner(options, "me");
	ast_sdp_options_set_rtp_engine(options, "asterisk");
	ast_sdp_options_set_impl(options, AST_SDP_IMPL_PJMEDIA);

	topology = ast_stream_topology_alloc();
	if (!topology) {
		goto end;
	}

	for (i = 0; i < num_streams; ++i) {
		RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
		struct ast_stream *stream;

		caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (!caps) {
			goto end;
		}
		if (ast_format_cap_update_by_allow_disallow(caps, formats[i].formats, 1) < 0) {
			goto end;
		}
		stream = ast_stream_alloc("sure_thing", formats[i].type);
		if (!stream) {
			goto end;
		}
		ast_stream_set_formats(stream, caps);
		ast_stream_topology_append_stream(topology, stream);
	}

	state = ast_sdp_state_alloc(topology, options);
	if (!state) {
		goto end;
	}

end:
	ast_stream_topology_free(topology);
	if (!state) {
		ast_sdp_options_free(options);
	}

	return state;
}

AST_TEST_DEFINE(topology_to_sdp)
{
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct ast_sdp_state *sdp_state = NULL;
	const struct ast_sdp *sdp = NULL;
	struct ast_sdp_m_line *m_line = NULL;
	struct sdp_format formats[] = {
		{ AST_MEDIA_TYPE_AUDIO, "ulaw,alaw,g722,opus" },
		{ AST_MEDIA_TYPE_VIDEO, "h264,vp8" },
	};

	switch(cmd) {
	case TEST_INIT:
		info->name = "topology_to_sdp";
		info->category = "/main/sdp/";
		info->summary = "Convert a topology into an SDP";
		info->description =
			"Ensure SDPs get converted to expected stream topology";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	sdp_state = build_sdp_state(ARRAY_LEN(formats), formats);
	if (!sdp_state) {
		goto end;
	}

	sdp = ast_sdp_state_get_local_sdp(sdp_state);
	if (!sdp) {
		goto end;
	}

	if (validate_o_line(test, sdp->o_line, "me", "IP4", "127.0.0.1")) {
		goto end;
	}

	if (validate_c_line(test, sdp->c_line, "IP4", "127.0.0.1")) {
		goto end;
	}

	if (ast_sdp_get_m_count(sdp) != 2) {
		ast_test_status_update(test, "Unexpected number of streams in generated SDP: %d\n",
			ast_sdp_get_m_count(sdp));
		goto end;
	}

	m_line = ast_sdp_get_m(sdp, 0);

	if (validate_m_line(test, m_line, "audio", 4)) {
		goto end;
	}

	if (validate_rtpmap(test, m_line, "PCMU")) {
		goto end;
	}

	if (validate_rtpmap(test, m_line, "PCMA")) {
		goto end;
	}

	if (validate_rtpmap(test, m_line, "G722")) {
		goto end;
	}

	if (validate_rtpmap(test, m_line, "opus")) {
		goto end;
	}

	m_line = ast_sdp_get_m(sdp, 1);
	if (validate_m_line(test, m_line, "video", 2)) {
		goto end;
	}

	if (validate_rtpmap(test, m_line, "VP8")) {
		goto end;
	}

	if (validate_rtpmap(test, m_line, "H264")) {
		goto end;
	}

	res = AST_TEST_PASS;

end:
	ast_sdp_state_free(sdp_state);
	return res;
}

static int validate_formats(struct ast_test *test, struct ast_stream_topology *topology, int index,
	enum ast_media_type type, int format_count, const char **expected_formats)
{
	struct ast_stream *stream;
	struct ast_format_cap *caps;
	struct ast_format *format;
	int i;

	stream = ast_stream_topology_get_stream(topology, index);
	if (ast_stream_get_type(stream) != type) {
		ast_test_status_update(test, "Unexpected stream type encountered\n");
		return -1;
	}
	caps = ast_stream_get_formats(stream);

	if (ast_format_cap_count(caps) != format_count) {
		ast_test_status_update(test, "Unexpected format count '%d'. Expecting '%d'\n",
			(int) ast_format_cap_count(caps), format_count);
		return -1;
	}

	for (i = 0; i < ast_format_cap_count(caps); ++i) {
		format = ast_format_cap_get_format(caps, i);
		if (strcmp(ast_format_get_name(format), expected_formats[i])) {
			ast_test_status_update(test, "Unexpected format '%s'at index %d. Expected '%s'\n",
				ast_format_get_name(format), i, expected_formats[i]);
			return -1;
		}
	}

	return 0;
}

AST_TEST_DEFINE(sdp_to_topology)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_sdp_state *sdp_state;
	const struct ast_sdp *sdp;
	struct ast_stream_topology *topology = NULL;
	struct sdp_format sdp_formats[] = {
		{ AST_MEDIA_TYPE_AUDIO, "ulaw,alaw,g722,opus" },
		{ AST_MEDIA_TYPE_VIDEO, "h264,vp8" },
	};
	static const char *expected_audio_formats[] = {
		"ulaw",
		"alaw",
		"g722",
		"opus",
	};
	static const char *expected_video_formats[] = {
		"h264",
		"vp8",
	};

	switch(cmd) {
	case TEST_INIT:
		info->name = "sdp_to_topology";
		info->category = "/main/sdp/";
		info->summary = "Convert an SDP into a topology";
		info->description =
			"Ensure SDPs get converted to expected stream topology";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	sdp_state = build_sdp_state(ARRAY_LEN(sdp_formats), sdp_formats);
	if (!sdp_state) {
		res = AST_TEST_FAIL;
		goto end;
	}

	sdp = ast_sdp_state_get_local_sdp(sdp_state);
	if (!sdp) {
		res = AST_TEST_FAIL;
		goto end;
	}

	topology = ast_get_topology_from_sdp(sdp);

	if (ast_stream_topology_get_count(topology) != 2) {
		ast_test_status_update(test, "Unexpected topology count '%d'. Expecting 2\n",
			ast_stream_topology_get_count(topology));
		res = AST_TEST_FAIL;
		goto end;
	}

	if (validate_formats(test, topology, 0, AST_MEDIA_TYPE_AUDIO,
			ARRAY_LEN(expected_audio_formats), expected_audio_formats)) {
		res = AST_TEST_FAIL;
		goto end;
	}

	if (validate_formats(test, topology, 1, AST_MEDIA_TYPE_VIDEO,
			ARRAY_LEN(expected_video_formats), expected_video_formats)) {
		res = AST_TEST_FAIL;
		goto end;
	}

end:
	ast_sdp_state_free(sdp_state);
	ast_stream_topology_free(topology);
	return res;
}

static int validate_merged_sdp(struct ast_test *test, const struct ast_sdp *sdp)
{
	struct ast_sdp_m_line *m_line;

	if (!sdp) {
		return -1;
	}

	m_line = ast_sdp_get_m(sdp, 0);

	if (validate_m_line(test, m_line, "audio", 1)) {
		return -1;
	}

	if (validate_rtpmap(test, m_line, "PCMU")) {
		return -1;
	}

	/* The other audio formats should *NOT* be present */
	if (!validate_rtpmap(test, m_line, "PCMA")) {
		return -1;
	}

	if (!validate_rtpmap(test, m_line, "G722")) {
		return -1;
	}

	if (!validate_rtpmap(test, m_line, "opus")) {
		return -1;
	}

	m_line = ast_sdp_get_m(sdp, 1);

	if (validate_m_line(test, m_line, "video", 1)) {
		return -1;
	}

	if (validate_rtpmap(test, m_line, "VP8")) {
		return -1;
	}

	if (!validate_rtpmap(test, m_line, "H264")) {
		return -1;
	}

	return 0;
}

AST_TEST_DEFINE(sdp_merge_symmetric)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_sdp_state *sdp_state_offerer = NULL;
	struct ast_sdp_state *sdp_state_answerer = NULL;
	const struct ast_sdp *offerer_sdp;
	const struct ast_sdp *answerer_sdp;

	static const struct sdp_format offerer_formats[] = {
		{ AST_MEDIA_TYPE_AUDIO, "ulaw,alaw,g722,opus" },
		{ AST_MEDIA_TYPE_VIDEO, "h264,vp8" },
	};
	static const struct sdp_format answerer_formats[] = {
		{ AST_MEDIA_TYPE_AUDIO, "ulaw" },
		{ AST_MEDIA_TYPE_VIDEO, "vp8" },
	};

	switch(cmd) {
	case TEST_INIT:
		info->name = "sdp_merge_symmetric";
		info->category = "/main/sdp/";
		info->summary = "Merge two SDPs with symmetric stream types";
		info->description =
			"SDPs 1 and 2 each have one audio and one video stream (in that order).\n"
			"SDP 1 offers to SDP 2, who answers. We ensure that both local SDPs have\n"
			"the expected stream types and the expected formats";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	sdp_state_offerer = build_sdp_state(ARRAY_LEN(offerer_formats), offerer_formats);
	if (!sdp_state_offerer) {
		res = AST_TEST_FAIL;
		goto end;
	}

	sdp_state_answerer = build_sdp_state(ARRAY_LEN(answerer_formats), answerer_formats);
	if (!sdp_state_answerer) {
		res = AST_TEST_FAIL;
		goto end;
	}

	offerer_sdp = ast_sdp_state_get_local_sdp(sdp_state_offerer);
	if (!offerer_sdp) {
		res = AST_TEST_FAIL;
		goto end;
	}

	ast_sdp_state_set_remote_sdp(sdp_state_answerer, offerer_sdp);
	answerer_sdp = ast_sdp_state_get_local_sdp(sdp_state_answerer);
	if (!answerer_sdp) {
		res = AST_TEST_FAIL;
		goto end;
	}

	ast_sdp_state_set_remote_sdp(sdp_state_offerer, answerer_sdp);

	/* Get the offerer SDP again because it's now going to be the joint SDP */
	offerer_sdp = ast_sdp_state_get_local_sdp(sdp_state_offerer);
	if (validate_merged_sdp(test, offerer_sdp)) {
		res = AST_TEST_FAIL;
		goto end;
	}
	if (validate_merged_sdp(test, answerer_sdp)) {
		res = AST_TEST_FAIL;
		goto end;
	}

end:
	ast_sdp_state_free(sdp_state_offerer);
	ast_sdp_state_free(sdp_state_answerer);

	return res;
}

AST_TEST_DEFINE(sdp_merge_crisscross)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_sdp_state *sdp_state_offerer = NULL;
	struct ast_sdp_state *sdp_state_answerer = NULL;
	const struct ast_sdp *offerer_sdp;
	const struct ast_sdp *answerer_sdp;

	static const struct sdp_format offerer_formats[] = {
		{ AST_MEDIA_TYPE_AUDIO, "ulaw,alaw,g722,opus" },
		{ AST_MEDIA_TYPE_VIDEO, "h264,vp8" },
	};
	static const struct sdp_format answerer_formats[] = {
		{ AST_MEDIA_TYPE_VIDEO, "vp8" },
		{ AST_MEDIA_TYPE_AUDIO, "ulaw" },
	};

	switch(cmd) {
	case TEST_INIT:
		info->name = "sdp_merge_crisscross";
		info->category = "/main/sdp/";
		info->summary = "Merge two SDPs with symmetric stream types";
		info->description =
			"SDPs 1 and 2 each have one audio and one video stream. However, SDP 1 and\n"
			"2 natively have the formats in a different order.\n"
			"SDP 1 offers to SDP 2, who answers. We ensure that both local SDPs have\n"
			"the expected stream types and the expected formats. Since SDP 1 was the\n"
			"offerer, the format order on SDP 1 should determine the order of formats in the SDPs";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	sdp_state_offerer = build_sdp_state(ARRAY_LEN(offerer_formats), offerer_formats);
	if (!sdp_state_offerer) {
		res = AST_TEST_FAIL;
		goto end;
	}

	sdp_state_answerer = build_sdp_state(ARRAY_LEN(answerer_formats), answerer_formats);
	if (!sdp_state_answerer) {
		res = AST_TEST_FAIL;
		goto end;
	}

	offerer_sdp = ast_sdp_state_get_local_sdp(sdp_state_offerer);
	if (!offerer_sdp) {
		res = AST_TEST_FAIL;
		goto end;
	}

	ast_sdp_state_set_remote_sdp(sdp_state_answerer, offerer_sdp);
	answerer_sdp = ast_sdp_state_get_local_sdp(sdp_state_answerer);
	if (!answerer_sdp) {
		res = AST_TEST_FAIL;
		goto end;
	}

	ast_sdp_state_set_remote_sdp(sdp_state_offerer, answerer_sdp);

	/* Get the offerer SDP again because it's now going to be the joint SDP */
	offerer_sdp = ast_sdp_state_get_local_sdp(sdp_state_offerer);
	if (validate_merged_sdp(test, offerer_sdp)) {
		res = AST_TEST_FAIL;
		goto end;
	}
	if (validate_merged_sdp(test, answerer_sdp)) {
		res = AST_TEST_FAIL;
		goto end;
	}

end:
	ast_sdp_state_free(sdp_state_offerer);
	ast_sdp_state_free(sdp_state_answerer);

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(invalid_rtpmap);
	AST_TEST_UNREGISTER(rtpmap);
	AST_TEST_UNREGISTER(find_attr);
	AST_TEST_UNREGISTER(topology_to_sdp);
	AST_TEST_UNREGISTER(sdp_to_topology);
	AST_TEST_UNREGISTER(sdp_merge_symmetric);
	AST_TEST_UNREGISTER(sdp_merge_crisscross);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(invalid_rtpmap);
	AST_TEST_REGISTER(rtpmap);
	AST_TEST_REGISTER(find_attr);
	AST_TEST_REGISTER(topology_to_sdp);
	AST_TEST_REGISTER(sdp_to_topology);
	AST_TEST_REGISTER(sdp_merge_symmetric);
	AST_TEST_REGISTER(sdp_merge_crisscross);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "SDP tests");
