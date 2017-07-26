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
#include "asterisk/rtp_engine.h"

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

	if (m_line->port == 0) {
		ast_test_status_update(test, "Expected %s m-line to not be declined\n",
			media_type);
		return -1;
	}

	if (ast_sdp_m_get_payload_count(m_line) != num_payloads) {
		ast_test_status_update(test, "Expected %s m-line payload count %d but got %d\n",
			media_type, num_payloads, ast_sdp_m_get_payload_count(m_line));
		return -1;
	}

	ast_test_status_update(test, "SDP %s m-line is as expected\n", media_type);
	return 0;
}

static int validate_m_line_declined(struct ast_test *test,
	const struct ast_sdp_m_line *m_line, const char *media_type)
{
	if (strcmp(m_line->type, media_type)) {
		ast_test_status_update(test, "Expected m-line media type %s but got %s\n",
			media_type, m_line->type);
		return -1;
	}

	if (m_line->port != 0) {
		ast_test_status_update(test, "Expected %s m-line to be declined but got port %u\n",
			media_type, m_line->port);
		return -1;
	}

	ast_test_status_update(test, "SDP %s m-line is as expected\n", media_type);
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

static enum ast_test_result_state validate_t38(struct ast_test *test, const struct ast_sdp_m_line *m_line)
{
	struct ast_sdp_a_line *a_line;

	a_line = ast_sdp_m_find_attribute(m_line, "T38FaxVersion", -1);
	ast_test_validate(test, a_line && !strcmp(a_line->value, "0"));

	a_line = ast_sdp_m_find_attribute(m_line, "T38FaxMaxBitRate", -1);
	ast_test_validate(test, a_line && !strcmp(a_line->value, "14400"));

	a_line = ast_sdp_m_find_attribute(m_line, "T38FaxRateManagement", -1);
	ast_test_validate(test, a_line && !strcmp(a_line->value, "transferredTCF"));

	return AST_TEST_PASS;
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
	int idx;

	switch(cmd) {
	case TEST_INIT:
		info->name = "find_attr";
		info->category = "/main/sdp/";
		info->summary = "Ensure that finding attributes works as expected";
		info->description =
			"A SDP m-line is created, and attributes are added.\n"
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
	a_line = ast_sdp_a_alloc("foo", "0 bee");
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
	if (!a_line || strcmp(a_line->value, "0 bar")) {
		ast_test_status_update(test, "Failed to find attribute 'foo' with payload '0'\n");
		res = AST_TEST_FAIL;
	}
	a_line = ast_sdp_m_find_attribute(m_line, "foo", -1);
	if (!a_line || strcmp(a_line->value, "0 bar")) {
		ast_test_status_update(test, "Failed to find attribute 'foo' with unspecified payload\n");
		res = AST_TEST_FAIL;
	}
	a_line = ast_sdp_m_find_attribute(m_line, "baz", -1);
	if (!a_line || strcmp(a_line->value, "howdy")) {
		ast_test_status_update(test, "Failed to find attribute 'baz' with unspecified payload\n");
		res = AST_TEST_FAIL;
	}

	idx = ast_sdp_m_find_a_first(m_line, "foo", 0);
	if (idx < 0) {
		ast_test_status_update(test, "Failed to find first attribute 'foo' with payload '0'\n");
		res = AST_TEST_FAIL;
		goto end;
	}
	a_line = ast_sdp_m_get_a(m_line, idx);
	if (!a_line || strcmp(a_line->value, "0 bar")) {
		ast_test_status_update(test, "Find first attribute 'foo' with payload '0' didn't match\n");
		res = AST_TEST_FAIL;
	}
	idx = ast_sdp_m_find_a_next(m_line, idx, "foo", 0);
	if (idx < 0) {
		ast_test_status_update(test, "Failed to find next attribute 'foo' with payload '0'\n");
		res = AST_TEST_FAIL;
		goto end;
	}
	a_line = ast_sdp_m_get_a(m_line, idx);
	if (!a_line || strcmp(a_line->value, "0 bee")) {
		ast_test_status_update(test, "Find next attribute 'foo' with payload '0' didn't match\n");
		res = AST_TEST_FAIL;
	}
	idx = ast_sdp_m_find_a_next(m_line, idx, "foo", 0);
	if (0 <= idx) {
		ast_test_status_update(test, "Find next attribute 'foo' with payload '0' found too many\n");
		res = AST_TEST_FAIL;
	}

	idx = ast_sdp_m_find_a_first(m_line, "foo", -1);
	if (idx < 0) {
		ast_test_status_update(test, "Failed to find first attribute 'foo' with unspecified payload\n");
		res = AST_TEST_FAIL;
		goto end;
	}
	a_line = ast_sdp_m_get_a(m_line, idx);
	if (!a_line || strcmp(a_line->value, "0 bar")) {
		ast_test_status_update(test, "Find first attribute 'foo' with unspecified payload didn't match\n");
		res = AST_TEST_FAIL;
	}
	idx = ast_sdp_m_find_a_next(m_line, idx, "foo", -1);
	if (idx < 0) {
		ast_test_status_update(test, "Failed to find next attribute 'foo' with unspecified payload\n");
		res = AST_TEST_FAIL;
		goto end;
	}
	a_line = ast_sdp_m_get_a(m_line, idx);
	if (!a_line || strcmp(a_line->value, "0 bee")) {
		ast_test_status_update(test, "Find next attribute 'foo' with unspecified payload didn't match\n");
		res = AST_TEST_FAIL;
	}
	idx = ast_sdp_m_find_a_next(m_line, idx, "foo", -1);
	if (0 <= idx) {
		ast_test_status_update(test, "Find next attribute 'foo' with unspecified payload found too many\n");
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
		ast_test_status_update(test, "Found non-existent attribute 'wibble' with unspecified payload\n");
		res = AST_TEST_FAIL;
	}

end:
	ast_sdp_m_free(m_line);
	return res;
}

static struct ast_sdp_options *sdp_options_common(void)
{
	struct ast_sdp_options *options;

	options = ast_sdp_options_alloc();
	if (!options) {
		return NULL;
	}
	ast_sdp_options_set_media_address(options, "127.0.0.1");
	ast_sdp_options_set_sdpowner(options, "me");
	ast_sdp_options_set_rtp_engine(options, "asterisk");
	ast_sdp_options_set_impl(options, AST_SDP_IMPL_PJMEDIA);

	return options;
}

struct sdp_format {
	enum ast_media_type type;
	const char *formats;
};

static int build_sdp_option_formats(struct ast_sdp_options *options, int num_streams, const struct sdp_format *formats)
{
	int idx;

	for (idx = 0; idx < num_streams; ++idx) {
		struct ast_format_cap *caps;

		if (ast_strlen_zero(formats[idx].formats)) {
			continue;
		}

		caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (!caps
			|| ast_format_cap_update_by_allow_disallow(caps, formats[idx].formats, 1) < 0) {
			ao2_cleanup(caps);
			return -1;
		}
		ast_sdp_options_set_format_cap_type(options, formats[idx].type, caps);
		ao2_cleanup(caps);
	}
	return 0;
}

/*!
 * \brief Common method to build an SDP state for a test.
 *
 * This uses the passed-in formats to create a stream topology, which is then used to create the SDP
 * state.
 *
 * There is an optional test_options field you can use if your test has specific options you need to
 * set. If your test does not require anything special, it can just pass NULL for this parameter. If
 * you do pass in test_options, this function steals ownership of those options.
 *
 * \param num_streams The number of elements in the formats array.
 * \param formats Array of media types and formats that will be in the state.
 * \param opt_num_streams The number of new stream types allowed to create.
 *           Not used if test_options provided.
 * \param opt_formats Array of new stream media types and formats allowed to create.
 *           NULL if use a default stream creation.
 *           Not used if test_options provided.
 * \param max_streams 0 if set max to max(3, num_streams) else max(max_streams, num_streams)
 *           Not used if test_options provided.
 * \param test_options Optional SDP options.
 */
static struct ast_sdp_state *build_sdp_state(int num_streams, const struct sdp_format *formats,
	int opt_num_streams, const struct sdp_format *opt_formats, unsigned int max_streams,
	struct ast_sdp_options *test_options)
{
	struct ast_stream_topology *topology = NULL;
	struct ast_sdp_state *state = NULL;
	struct ast_sdp_options *options;
	int i;

	if (!test_options) {
		static const struct sdp_format sdp_formats[] = {
			{ AST_MEDIA_TYPE_AUDIO, "ulaw" },
			{ AST_MEDIA_TYPE_VIDEO, "vp8" },
			{ AST_MEDIA_TYPE_IMAGE, "t38" },
		};

		options = sdp_options_common();
		if (!options) {
			goto end;
		}

		/* Determine max_streams to allow */
		if (!max_streams) {
			max_streams = ARRAY_LEN(sdp_formats);
		}
		if (max_streams < num_streams) {
			max_streams = num_streams;
		}
		ast_sdp_options_set_max_streams(options, max_streams);

		/* Determine new stream formats and types allowed */
		if (!opt_formats) {
			opt_num_streams = ARRAY_LEN(sdp_formats);
			opt_formats = sdp_formats;
		}
		if (build_sdp_option_formats(options, opt_num_streams, opt_formats)) {
			goto end;
		}
	} else {
		options = test_options;
	}

	topology = ast_stream_topology_alloc();
	if (!topology) {
		goto end;
	}

	for (i = 0; i < num_streams; ++i) {
		struct ast_stream *stream;

		stream = ast_stream_alloc("sure_thing", formats[i].type);
		if (!stream) {
			goto end;
		}
		if (!ast_strlen_zero(formats[i].formats)) {
			struct ast_format_cap *caps;

			caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
			if (!caps
				|| ast_format_cap_update_by_allow_disallow(caps, formats[i].formats, 1) < 0) {
				ao2_cleanup(caps);
				ast_stream_free(stream);
				goto end;
			}
			ast_stream_set_formats(stream, caps);
			ao2_cleanup(caps);
		} else {
			ast_stream_set_state(stream, AST_STREAM_STATE_REMOVED);
		}
		if (ast_stream_topology_append_stream(topology, stream) < 0) {
			ast_stream_free(stream);
			goto end;
		}
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
		{ AST_MEDIA_TYPE_IMAGE, "t38" },
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

	sdp_state = build_sdp_state(ARRAY_LEN(formats), formats,
		ARRAY_LEN(formats), formats, 0, NULL);
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

	if (ast_sdp_get_m_count(sdp) != 3) {
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

	m_line = ast_sdp_get_m(sdp, 2);
	if (validate_m_line(test, m_line, "image", 1)) {
		goto end;
	}
	if (validate_t38(test, m_line) != AST_TEST_PASS) {
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
		{ AST_MEDIA_TYPE_IMAGE, "t38" },
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
	static const char *expected_image_formats[] = {
		"t38",
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

	sdp_state = build_sdp_state(ARRAY_LEN(sdp_formats), sdp_formats,
		ARRAY_LEN(sdp_formats), sdp_formats, 0, NULL);
	if (!sdp_state) {
		res = AST_TEST_FAIL;
		goto end;
	}

	sdp = ast_sdp_state_get_local_sdp(sdp_state);
	if (!sdp) {
		res = AST_TEST_FAIL;
		goto end;
	}

	topology = ast_get_topology_from_sdp(sdp, 0);
	if (!topology) {
		res = AST_TEST_FAIL;
		goto end;
	}

	if (ast_stream_topology_get_count(topology) != 3) {
		ast_test_status_update(test, "Unexpected topology count '%d'. Expecting 3\n",
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

	if (validate_formats(test, topology, 2, AST_MEDIA_TYPE_IMAGE,
			ARRAY_LEN(expected_image_formats), expected_image_formats)) {
		res = AST_TEST_FAIL;
		goto end;
	}

end:
	ast_sdp_state_free(sdp_state);
	ast_stream_topology_free(topology);
	return res;
}

static int validate_avi_sdp_streams(struct ast_test *test, const struct ast_sdp *sdp)
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

	m_line = ast_sdp_get_m(sdp, 2);
	if (validate_m_line(test, m_line, "image", 1)) {
		return -1;
	}

	return 0;
}

static enum ast_test_result_state sdp_negotiation_completed_tests(struct ast_test *test,
	int offer_num_streams, const struct sdp_format *offer_formats,
	int answer_num_streams, const struct sdp_format *answer_formats,
	int allowed_ans_num_streams, const struct sdp_format *allowed_ans_formats,
	unsigned int max_streams,
	int (*validate_sdp)(struct ast_test *test, const struct ast_sdp *sdp))
{
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_sdp_state *sdp_state_offerer = NULL;
	struct ast_sdp_state *sdp_state_answerer = NULL;
	const struct ast_sdp *offerer_sdp;
	const struct ast_sdp *answerer_sdp;

	sdp_state_offerer = build_sdp_state(offer_num_streams, offer_formats,
		offer_num_streams, offer_formats, max_streams, NULL);
	if (!sdp_state_offerer) {
		ast_test_status_update(test, "Building offerer SDP state failed\n");
		res = AST_TEST_FAIL;
		goto end;
	}

	sdp_state_answerer = build_sdp_state(answer_num_streams, answer_formats,
		allowed_ans_num_streams, allowed_ans_formats, max_streams, NULL);
	if (!sdp_state_answerer) {
		ast_test_status_update(test, "Building answerer SDP state failed\n");
		res = AST_TEST_FAIL;
		goto end;
	}

	offerer_sdp = ast_sdp_state_get_local_sdp(sdp_state_offerer);
	if (!offerer_sdp) {
		ast_test_status_update(test, "Building offerer offer failed\n");
		res = AST_TEST_FAIL;
		goto end;
	}

	if (ast_sdp_state_set_remote_sdp(sdp_state_answerer, offerer_sdp)) {
		ast_test_status_update(test, "Setting answerer offer failed\n");
		res = AST_TEST_FAIL;
		goto end;
	}
	answerer_sdp = ast_sdp_state_get_local_sdp(sdp_state_answerer);
	if (!answerer_sdp) {
		ast_test_status_update(test, "Building answerer answer failed\n");
		res = AST_TEST_FAIL;
		goto end;
	}

	if (ast_sdp_state_set_remote_sdp(sdp_state_offerer, answerer_sdp)) {
		ast_test_status_update(test, "Setting offerer answer failed\n");
		res = AST_TEST_FAIL;
		goto end;
	}

	/*
	 * Restart SDP negotiations to build the joint SDP on the offerer
	 * side.  Otherwise we will get the original offer for use in
	 * case of retransmissions.
	 */
	if (ast_sdp_state_restart_negotiations(sdp_state_offerer)) {
		ast_test_status_update(test, "Restarting negotiations failed\n");
		res = AST_TEST_FAIL;
		goto end;
	}
	offerer_sdp = ast_sdp_state_get_local_sdp(sdp_state_offerer);
	if (!offerer_sdp) {
		ast_test_status_update(test, "Building offerer current sdp failed\n");
		res = AST_TEST_FAIL;
		goto end;
	}
	if (validate_sdp(test, offerer_sdp)) {
		res = AST_TEST_FAIL;
		goto end;
	}
	if (validate_sdp(test, answerer_sdp)) {
		res = AST_TEST_FAIL;
		goto end;
	}

end:
	ast_sdp_state_free(sdp_state_offerer);
	ast_sdp_state_free(sdp_state_answerer);

	return res;
}

AST_TEST_DEFINE(sdp_negotiation_initial)
{
	static const struct sdp_format offerer_formats[] = {
		{ AST_MEDIA_TYPE_AUDIO, "ulaw,alaw,g722,opus" },
		{ AST_MEDIA_TYPE_VIDEO, "h264,vp8" },
		{ AST_MEDIA_TYPE_IMAGE, "t38" },
	};

	switch(cmd) {
	case TEST_INIT:
		info->name = "sdp_negotiation_initial";
		info->category = "/main/sdp/";
		info->summary = "Simulate an initial negotiation";
		info->description =
			"Initial negotiation tests creating new streams on the answering side.\n"
			"After negotiation both offerer and answerer sides should have the same\n"
			"expected stream types and formats.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return sdp_negotiation_completed_tests(test,
		ARRAY_LEN(offerer_formats), offerer_formats,
		0, NULL,
		0, NULL,
		0,
		validate_avi_sdp_streams);
}

AST_TEST_DEFINE(sdp_negotiation_type_change)
{
	static const struct sdp_format offerer_formats[] = {
		{ AST_MEDIA_TYPE_AUDIO, "ulaw,alaw,g722,opus" },
		{ AST_MEDIA_TYPE_VIDEO, "h264,vp8" },
		{ AST_MEDIA_TYPE_IMAGE, "t38" },
	};
	static const struct sdp_format answerer_formats[] = {
		{ AST_MEDIA_TYPE_IMAGE, "t38" },
		{ AST_MEDIA_TYPE_VIDEO, "vp8" },
		{ AST_MEDIA_TYPE_AUDIO, "ulaw" },
	};

	switch(cmd) {
	case TEST_INIT:
		info->name = "sdp_negotiation_type_change";
		info->category = "/main/sdp/";
		info->summary = "Simulate a re-negotiation changing stream types";
		info->description =
			"Reinvite negotiation tests changing stream types on the answering side.\n"
			"After negotiation both offerer and answerer sides should have the same\n"
			"expected stream types and formats.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return sdp_negotiation_completed_tests(test,
		ARRAY_LEN(offerer_formats), offerer_formats,
		ARRAY_LEN(answerer_formats), answerer_formats,
		0, NULL,
		0,
		validate_avi_sdp_streams);
}

static int validate_aviavia_declined_sdp_streams(struct ast_test *test, const struct ast_sdp *sdp)
{
	struct ast_sdp_m_line *m_line;

	if (!sdp) {
		return -1;
	}

	m_line = ast_sdp_get_m(sdp, 0);
	if (validate_m_line_declined(test, m_line, "audio")) {
		return -1;
	}

	m_line = ast_sdp_get_m(sdp, 1);
	if (validate_m_line_declined(test, m_line, "video")) {
		return -1;
	}

	m_line = ast_sdp_get_m(sdp, 2);
	if (validate_m_line_declined(test, m_line, "image")) {
		return -1;
	}

	m_line = ast_sdp_get_m(sdp, 3);
	if (validate_m_line_declined(test, m_line, "audio")) {
		return -1;
	}

	m_line = ast_sdp_get_m(sdp, 4);
	if (validate_m_line_declined(test, m_line, "video")) {
		return -1;
	}

	m_line = ast_sdp_get_m(sdp, 5);
	if (validate_m_line_declined(test, m_line, "image")) {
		return -1;
	}

	m_line = ast_sdp_get_m(sdp, 6);
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

	return 0;
}

AST_TEST_DEFINE(sdp_negotiation_decline_incompatible)
{
	static const struct sdp_format offerer_formats[] = {
		/* Incompatible declined streams */
		{ AST_MEDIA_TYPE_AUDIO, "alaw" },
		{ AST_MEDIA_TYPE_VIDEO, "vp8" },
		{ AST_MEDIA_TYPE_IMAGE, "t38" },
		/* Initially declined streams */
		{ AST_MEDIA_TYPE_AUDIO, "" },
		{ AST_MEDIA_TYPE_VIDEO, "" },
		{ AST_MEDIA_TYPE_IMAGE, "" },
		/* Compatible stream so not all are declined */
		{ AST_MEDIA_TYPE_AUDIO, "ulaw,alaw" },
	};
	static const struct sdp_format allowed_formats[] = {
		{ AST_MEDIA_TYPE_AUDIO, "ulaw" },
	};

	switch(cmd) {
	case TEST_INIT:
		info->name = "sdp_negotiation_decline_incompatible";
		info->category = "/main/sdp/";
		info->summary = "Simulate an initial negotiation declining streams";
		info->description =
			"Initial negotiation tests declining incompatible streams.\n"
			"After negotiation both offerer and answerer sides should have\n"
			"the same expected stream types and formats.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return sdp_negotiation_completed_tests(test,
		ARRAY_LEN(offerer_formats), offerer_formats,
		0, NULL,
		ARRAY_LEN(allowed_formats), allowed_formats,
		ARRAY_LEN(offerer_formats),
		validate_aviavia_declined_sdp_streams);
}

static int validate_aaaa_declined_sdp_streams(struct ast_test *test, const struct ast_sdp *sdp)
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

	m_line = ast_sdp_get_m(sdp, 1);
	if (validate_m_line(test, m_line, "audio", 1)) {
		return -1;
	}
	if (validate_rtpmap(test, m_line, "PCMU")) {
		return -1;
	}

	m_line = ast_sdp_get_m(sdp, 2);
	if (validate_m_line(test, m_line, "audio", 1)) {
		return -1;
	}
	if (validate_rtpmap(test, m_line, "PCMU")) {
		return -1;
	}

	m_line = ast_sdp_get_m(sdp, 3);
	if (validate_m_line_declined(test, m_line, "audio")) {
		return -1;
	}

	return 0;
}

AST_TEST_DEFINE(sdp_negotiation_decline_max_streams)
{
	static const struct sdp_format offerer_formats[] = {
		{ AST_MEDIA_TYPE_AUDIO, "ulaw" },
		{ AST_MEDIA_TYPE_AUDIO, "ulaw" },
		{ AST_MEDIA_TYPE_AUDIO, "ulaw" },
		{ AST_MEDIA_TYPE_AUDIO, "ulaw" },
	};

	switch(cmd) {
	case TEST_INIT:
		info->name = "sdp_negotiation_decline_max_streams";
		info->category = "/main/sdp/";
		info->summary = "Simulate an initial negotiation declining excessive streams";
		info->description =
			"Initial negotiation tests declining too many streams on the answering side.\n"
			"After negotiation both offerer and answerer sides should have the same\n"
			"expected stream types and formats.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return sdp_negotiation_completed_tests(test,
		ARRAY_LEN(offerer_formats), offerer_formats,
		0, NULL,
		0, NULL,
		0,
		validate_aaaa_declined_sdp_streams);
}

AST_TEST_DEFINE(sdp_negotiation_not_acceptable)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_sdp_state *sdp_state_offerer = NULL;
	struct ast_sdp_state *sdp_state_answerer = NULL;
	const struct ast_sdp *offerer_sdp;

	static const struct sdp_format offerer_formats[] = {
		{ AST_MEDIA_TYPE_AUDIO, "alaw" },
		{ AST_MEDIA_TYPE_AUDIO, "alaw" },
	};

	switch(cmd) {
	case TEST_INIT:
		info->name = "sdp_negotiation_not_acceptable";
		info->category = "/main/sdp/";
		info->summary = "Simulate an initial negotiation declining all streams";
		info->description =
			"Initial negotiation tests declining all streams for a 488 on the answering side.\n"
			"Negotiations should fail because there are no acceptable streams.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	sdp_state_offerer = build_sdp_state(ARRAY_LEN(offerer_formats), offerer_formats,
		ARRAY_LEN(offerer_formats), offerer_formats, 0, NULL);
	if (!sdp_state_offerer) {
		res = AST_TEST_FAIL;
		goto end;
	}

	sdp_state_answerer = build_sdp_state(0, NULL, 0, NULL, 0, NULL);
	if (!sdp_state_answerer) {
		res = AST_TEST_FAIL;
		goto end;
	}

	offerer_sdp = ast_sdp_state_get_local_sdp(sdp_state_offerer);
	if (!offerer_sdp) {
		res = AST_TEST_FAIL;
		goto end;
	}

	if (!ast_sdp_state_set_remote_sdp(sdp_state_answerer, offerer_sdp)) {
		ast_test_status_update(test, "Bad.  Setting remote SDP was successful.\n");
		res = AST_TEST_FAIL;
		goto end;
	}
	if (!ast_sdp_state_is_offer_rejected(sdp_state_answerer)) {
		ast_test_status_update(test, "Bad.  Negotiation failed for some other reason.\n");
		res = AST_TEST_FAIL;
		goto end;
	}

end:
	ast_sdp_state_free(sdp_state_offerer);
	ast_sdp_state_free(sdp_state_answerer);

	return res;
}

static int validate_ssrc(struct ast_test *test, struct ast_sdp_m_line *m_line,
	struct ast_rtp_instance *rtp)
{
	unsigned int ssrc;
	const char *cname;
	struct ast_sdp_a_line *a_line;
	char attr_value[128];

	ssrc = ast_rtp_instance_get_ssrc(rtp);
	cname = ast_rtp_instance_get_cname(rtp);

	snprintf(attr_value, sizeof(attr_value), "%u cname:%s", ssrc, cname);

	a_line = ast_sdp_m_find_attribute(m_line, "ssrc", -1);
	if (!a_line) {
		ast_test_status_update(test, "Could not find 'ssrc' attribute\n");
		return -1;
	}

	if (strcmp(a_line->value, attr_value)) {
		ast_test_status_update(test, "SDP attribute '%s' did not match expected attribute '%s'\n",
			a_line->value, attr_value);
		return -1;
	}

	return 0;
}

AST_TEST_DEFINE(sdp_ssrc_attributes)
{
	enum ast_test_result_state res;
	struct ast_sdp_state *test_state = NULL;
	struct ast_sdp_options *options;
	struct sdp_format formats[] = {
		{ AST_MEDIA_TYPE_AUDIO, "ulaw,alaw,g722,opus" },
	};
	const struct ast_sdp *sdp;
	struct ast_sdp_m_line *m_line;
	struct ast_rtp_instance *rtp;

	switch(cmd) {
	case TEST_INIT:
		info->name = "sdp_ssrc_attributes";
		info->category = "/main/sdp/";
		info->summary = "Ensure SSRC-level attributes are added to local SDPs";
		info->description =
			"An SDP is created and is instructed to include SSRC-level attributes.\n"
			"This test ensures that the CNAME SSRC-level attribute is present and\n"
			"that the values match what the RTP instance reports";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	res = AST_TEST_FAIL;

	options = sdp_options_common();
	if (!options) {
		ast_test_status_update(test, "Failed to allocate SDP options\n");
		goto end;
	}
	if (build_sdp_option_formats(options, ARRAY_LEN(formats), formats)) {
		goto end;
	}
	ast_sdp_options_set_ssrc(options, 1);

	test_state = build_sdp_state(ARRAY_LEN(formats), formats, 0, NULL, 0, options);
	if (!test_state) {
		ast_test_status_update(test, "Failed to create SDP state\n");
		goto end;
	}

	sdp = ast_sdp_state_get_local_sdp(test_state);
	if (!sdp) {
		ast_test_status_update(test, "Failed to get local SDP\n");
		goto end;
	}

	/* Need a couple of sanity checks */
	if (ast_sdp_get_m_count(sdp) != ARRAY_LEN(formats)) {
		ast_test_status_update(test, "SDP m count is %d instead of %zu\n",
			ast_sdp_get_m_count(sdp), ARRAY_LEN(formats));
		goto end;
	}

	m_line = ast_sdp_get_m(sdp, 0);
	if (!m_line) {
		ast_test_status_update(test, "Failed to get SDP m-line\n");
		goto end;
	}

	rtp = ast_sdp_state_get_rtp_instance(test_state, 0);
	if (!rtp) {
		ast_test_status_update(test, "Failed to get the RTP instance\n");
		goto end;
	}

	if (validate_ssrc(test, m_line, rtp)) {
		goto end;
	}

	res = AST_TEST_PASS;

end:
	ast_sdp_state_free(test_state);
	return res;
}

struct sdp_topology_stream {
	/*! Media stream type: audio, video, image */
	enum ast_media_type type;
	/*! Media stream state: removed/declined, sendrecv */
	enum ast_stream_state state;
	/*! Comma separated list of formats allowed on the stream.  Can be NULL if stream is removed/declined. */
	const char *formats;
	/*! Optional name of stream.  NULL for default name. */
	const char *name;
};

struct sdp_update_test {
	/*! Maximum number of streams.  (0 if default) */
	int max_streams;
	/*! Optional initial SDP state topology (NULL if not present) */
	const struct sdp_topology_stream * const *initial;
	/*! Required first topology update */
	const struct sdp_topology_stream * const *update_1;
	/*! Optional second topology update (NULL if not present) */
	const struct sdp_topology_stream * const *update_2;
	/*! Expected topology to be offered */
	const struct sdp_topology_stream * const *expected;
};

static struct ast_stream_topology *build_update_topology(const struct sdp_topology_stream * const *spec)
{
	struct ast_stream_topology *topology;
	const struct sdp_topology_stream *desc;

	topology = ast_stream_topology_alloc();
	if (!topology) {
		return NULL;
	}

	for (desc = *spec; desc; ++spec, desc = *spec) {
		struct ast_stream *stream;
		const char *name;

		name = desc->name ?: ast_codec_media_type2str(desc->type);
		stream = ast_stream_alloc(name, desc->type);
		if (!stream) {
			goto fail;
		}
		ast_stream_set_state(stream, desc->state);
		if (desc->formats) {
			struct ast_format_cap *caps;

			caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
			if (!caps
				|| ast_format_cap_update_by_allow_disallow(caps, desc->formats, 1) < 0) {
				ao2_cleanup(caps);
				ast_stream_free(stream);
				goto fail;
			}
			ast_stream_set_formats(stream, caps);
			ao2_ref(caps, -1);
		}
		if (ast_stream_topology_append_stream(topology, stream) < 0) {
			ast_stream_free(stream);
			goto fail;
		}
	}
	return topology;

fail:
	ast_stream_topology_free(topology);
	return NULL;
}

static int cmp_update_topology(struct ast_test *test,
	const struct ast_stream_topology *expected, const struct ast_stream_topology *merged)
{
	int status = 0;
	int idx;
	int max_streams;
	struct ast_stream *exp_stream;
	struct ast_stream *mrg_stream;

	idx = ast_stream_topology_get_count(expected);
	max_streams = ast_stream_topology_get_count(merged);
	if (idx != max_streams) {
		ast_test_status_update(test, "Expected %d streams got %d streams\n",
			idx, max_streams);
		status = -1;
	}
	if (idx < max_streams) {
		max_streams = idx;
	}

	/* Compare common streams by position */
	for (idx = 0; idx < max_streams; ++idx) {
		exp_stream = ast_stream_topology_get_stream(expected, idx);
		mrg_stream = ast_stream_topology_get_stream(merged, idx);

		if (strcmp(ast_stream_get_name(exp_stream), ast_stream_get_name(mrg_stream))) {
			ast_test_status_update(test,
				"Stream %d: Expected stream name '%s' got stream name '%s'\n",
				idx,
				ast_stream_get_name(exp_stream),
				ast_stream_get_name(mrg_stream));
			status = -1;
		}

		if (ast_stream_get_state(exp_stream) != ast_stream_get_state(mrg_stream)) {
			ast_test_status_update(test,
				"Stream %d: Expected stream state '%s' got stream state '%s'\n",
				idx,
				ast_stream_state2str(ast_stream_get_state(exp_stream)),
				ast_stream_state2str(ast_stream_get_state(mrg_stream)));
			status = -1;
		}

		if (ast_stream_get_type(exp_stream) != ast_stream_get_type(mrg_stream)) {
			ast_test_status_update(test,
				"Stream %d: Expected stream type '%s' got stream type '%s'\n",
				idx,
				ast_codec_media_type2str(ast_stream_get_type(exp_stream)),
				ast_codec_media_type2str(ast_stream_get_type(mrg_stream)));
			status = -1;
			continue;
		}

		if (ast_stream_get_state(exp_stream) == AST_STREAM_STATE_REMOVED
			|| ast_stream_get_state(mrg_stream) == AST_STREAM_STATE_REMOVED) {
			/*
			 * Cannot compare formats if one of the streams is
			 * declined because there may not be any on the declined
			 * stream.
			 */
			continue;
		}
		if (!ast_format_cap_identical(ast_stream_get_formats(exp_stream),
			ast_stream_get_formats(mrg_stream))) {
			ast_test_status_update(test,
				"Stream %d: Expected formats do not match merged formats\n",
				idx);
			status = -1;
		}
	}

	return status;
}


static const struct sdp_topology_stream audio_declined_no_name = {
	AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_REMOVED, NULL, NULL
};

static const struct sdp_topology_stream audio_ulaw_no_name = {
	AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, "ulaw", NULL
};

static const struct sdp_topology_stream audio_alaw_no_name = {
	AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, "alaw", NULL
};

static const struct sdp_topology_stream audio_g722_no_name = {
	AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, "g722", NULL
};

static const struct sdp_topology_stream audio_g723_no_name = {
	AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, "g723", NULL
};

static const struct sdp_topology_stream video_declined_no_name = {
	AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_REMOVED, NULL, NULL
};

static const struct sdp_topology_stream video_h261_no_name = {
	AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, "h261", NULL
};

static const struct sdp_topology_stream video_h263_no_name = {
	AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, "h263", NULL
};

static const struct sdp_topology_stream video_h264_no_name = {
	AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, "h264", NULL
};

static const struct sdp_topology_stream video_vp8_no_name = {
	AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, "vp8", NULL
};

static const struct sdp_topology_stream image_declined_no_name = {
	AST_MEDIA_TYPE_IMAGE, AST_STREAM_STATE_REMOVED, NULL, NULL
};

static const struct sdp_topology_stream image_t38_no_name = {
	AST_MEDIA_TYPE_IMAGE, AST_STREAM_STATE_SENDRECV, "t38", NULL
};


static const struct sdp_topology_stream *top_ulaw_alaw_h264__vp8[] = {
	&audio_ulaw_no_name,
	&audio_alaw_no_name,
	&video_h264_no_name,
	&video_vp8_no_name,
	NULL
};

static const struct sdp_topology_stream *top__vp8_alaw_h264_ulaw[] = {
	&video_vp8_no_name,
	&audio_alaw_no_name,
	&video_h264_no_name,
	&audio_ulaw_no_name,
	NULL
};

static const struct sdp_topology_stream *top_alaw_ulaw__vp8_h264[] = {
	&audio_alaw_no_name,
	&audio_ulaw_no_name,
	&video_vp8_no_name,
	&video_h264_no_name,
	NULL
};

/* Sorting by type with no new or deleted streams */
static const struct sdp_update_test mrg_by_type_00 = {
	.initial  = top_ulaw_alaw_h264__vp8,
	.update_1 = top__vp8_alaw_h264_ulaw,
	.expected = top_alaw_ulaw__vp8_h264,
};


static const struct sdp_topology_stream *top_alaw__vp8[] = {
	&audio_alaw_no_name,
	&video_vp8_no_name,
	NULL
};

static const struct sdp_topology_stream *top_h264__vp8_ulaw[] = {
	&video_h264_no_name,
	&video_vp8_no_name,
	&audio_ulaw_no_name,
	NULL
};

static const struct sdp_topology_stream *top_ulaw_h264__vp8[] = {
	&audio_ulaw_no_name,
	&video_h264_no_name,
	&video_vp8_no_name,
	NULL
};

/* Sorting by type and adding a stream */
static const struct sdp_update_test mrg_by_type_01 = {
	.initial  = top_alaw__vp8,
	.update_1 = top_h264__vp8_ulaw,
	.expected = top_ulaw_h264__vp8,
};


static const struct sdp_topology_stream *top_alaw__vp8_vdec[] = {
	&audio_alaw_no_name,
	&video_vp8_no_name,
	&video_declined_no_name,
	NULL
};

/* Sorting by type and deleting a stream */
static const struct sdp_update_test mrg_by_type_02 = {
	.initial  = top_ulaw_h264__vp8,
	.update_1 = top_alaw__vp8,
	.expected = top_alaw__vp8_vdec,
};


static const struct sdp_topology_stream *top_h264_alaw_ulaw[] = {
	&video_h264_no_name,
	&audio_alaw_no_name,
	&audio_ulaw_no_name,
	NULL
};

static const struct sdp_topology_stream *top__t38[] = {
	&image_t38_no_name,
	NULL
};

static const struct sdp_topology_stream *top_vdec__t38_adec[] = {
	&video_declined_no_name,
	&image_t38_no_name,
	&audio_declined_no_name,
	NULL
};

/* Sorting by type changing stream types for T.38 */
static const struct sdp_update_test mrg_by_type_03 = {
	.initial  = top_h264_alaw_ulaw,
	.update_1 = top__t38,
	.expected = top_vdec__t38_adec,
};


/* Sorting by type changing stream types back from T.38 */
static const struct sdp_update_test mrg_by_type_04 = {
	.initial  = top_vdec__t38_adec,
	.update_1 = top_h264_alaw_ulaw,
	.expected = top_h264_alaw_ulaw,
};


static const struct sdp_topology_stream *top_h264[] = {
	&video_h264_no_name,
	NULL
};

static const struct sdp_topology_stream *top_vdec__t38[] = {
	&video_declined_no_name,
	&image_t38_no_name,
	NULL
};

/* Sorting by type changing stream types for T.38 */
static const struct sdp_update_test mrg_by_type_05 = {
	.initial  = top_h264,
	.update_1 = top__t38,
	.expected = top_vdec__t38,
};


static const struct sdp_topology_stream *top_h264_idec[] = {
	&video_h264_no_name,
	&image_declined_no_name,
	NULL
};

/* Sorting by type changing stream types back from T.38 */
static const struct sdp_update_test mrg_by_type_06 = {
	.initial  = top_vdec__t38,
	.update_1 = top_h264,
	.expected = top_h264_idec,
};


static const struct sdp_topology_stream *top_ulaw_adec_h264__vp8[] = {
	&audio_ulaw_no_name,
	&audio_declined_no_name,
	&video_h264_no_name,
	&video_vp8_no_name,
	NULL
};

static const struct sdp_topology_stream *top_h263_alaw_h261_h264_vp8[] = {
	&video_h263_no_name,
	&audio_alaw_no_name,
	&video_h261_no_name,
	&video_h264_no_name,
	&video_vp8_no_name,
	NULL
};

static const struct sdp_topology_stream *top_alaw_h264_h263_h261_vp8[] = {
	&audio_alaw_no_name,
	&video_h264_no_name,
	&video_h263_no_name,
	&video_h261_no_name,
	&video_vp8_no_name,
	NULL
};

/* Sorting by type with backfill and adding streams */
static const struct sdp_update_test mrg_by_type_07 = {
	.initial  = top_ulaw_adec_h264__vp8,
	.update_1 = top_h263_alaw_h261_h264_vp8,
	.expected = top_alaw_h264_h263_h261_vp8,
};


static const struct sdp_topology_stream *top_ulaw_alaw_h264__vp8_h261[] = {
	&audio_ulaw_no_name,
	&audio_alaw_no_name,
	&video_h264_no_name,
	&video_vp8_no_name,
	&video_h261_no_name,
	NULL
};

/* Sorting by type overlimit of 4 and drop */
static const struct sdp_update_test mrg_by_type_08 = {
	.max_streams = 4,
	.initial  = top_ulaw_alaw_h264__vp8,
	.update_1 = top_ulaw_alaw_h264__vp8_h261,
	.expected = top_ulaw_alaw_h264__vp8,
};


static const struct sdp_topology_stream *top_ulaw_alaw_h264[] = {
	&audio_ulaw_no_name,
	&audio_alaw_no_name,
	&video_h264_no_name,
	NULL
};

static const struct sdp_topology_stream *top_alaw_h261__vp8[] = {
	&audio_alaw_no_name,
	&video_h261_no_name,
	&video_vp8_no_name,
	NULL
};

static const struct sdp_topology_stream *top_alaw_adec_h261__vp8[] = {
	&audio_alaw_no_name,
	&audio_declined_no_name,
	&video_h261_no_name,
	&video_vp8_no_name,
	NULL
};

/* Sorting by type with delete and add of streams */
static const struct sdp_update_test mrg_by_type_09 = {
	.initial  = top_ulaw_alaw_h264,
	.update_1 = top_alaw_h261__vp8,
	.expected = top_alaw_adec_h261__vp8,
};


static const struct sdp_topology_stream *top_ulaw_adec_h264[] = {
	&audio_ulaw_no_name,
	&audio_declined_no_name,
	&video_h264_no_name,
	NULL
};

/* Sorting by type and adding streams */
static const struct sdp_update_test mrg_by_type_10 = {
	.initial  = top_ulaw_adec_h264,
	.update_1 = top_alaw_ulaw__vp8_h264,
	.expected = top_alaw_ulaw__vp8_h264,
};


static const struct sdp_topology_stream *top_adec_g722_h261[] = {
	&audio_declined_no_name,
	&audio_g722_no_name,
	&video_h261_no_name,
	NULL
};

/* Sorting by type and deleting old streams */
static const struct sdp_update_test mrg_by_type_11 = {
	.initial  = top_ulaw_alaw_h264,
	.update_1 = top_adec_g722_h261,
	.expected = top_adec_g722_h261,
};


static const struct sdp_topology_stream audio_alaw4dave = {
	AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, "alaw", "dave"
};

static const struct sdp_topology_stream audio_g7224dave = {
	AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, "g722", "dave"
};

static const struct sdp_topology_stream audio_ulaw4fred = {
	AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, "ulaw", "fred"
};

static const struct sdp_topology_stream audio_alaw4fred = {
	AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, "alaw", "fred"
};

static const struct sdp_topology_stream audio_ulaw4rose = {
	AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, "ulaw", "rose"
};

static const struct sdp_topology_stream audio_g7224rose = {
	AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, "g722", "rose"
};


static const struct sdp_topology_stream video_h2614dave = {
	AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, "h261", "dave"
};

static const struct sdp_topology_stream video_h2634dave = {
	AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, "h263", "dave"
};

static const struct sdp_topology_stream video_h2634fred = {
	AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, "h263", "fred"
};

static const struct sdp_topology_stream video_h2644fred = {
	AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, "h264", "fred"
};

static const struct sdp_topology_stream video_h2644rose = {
	AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, "h264", "rose"
};

static const struct sdp_topology_stream video_h2614rose = {
	AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, "h261", "rose"
};


static const struct sdp_topology_stream *top_adave_alaw_afred_ulaw_arose_g722_vdave_h261_vfred_h263_vrose_h264[] = {
	&audio_alaw4dave,
	&audio_alaw_no_name,
	&audio_ulaw4fred,
	&audio_ulaw_no_name,
	&audio_g7224rose,
	&audio_g722_no_name,
	&video_h2614dave,
	&video_h261_no_name,
	&video_h2634fred,
	&video_h263_no_name,
	&video_h2644rose,
	&video_h264_no_name,
	NULL
};

static const struct sdp_topology_stream *top_vfred_vrose_vdave_h263_h264_h261_afred_ulaw_arose_g722_adave_alaw[] = {
	&video_h2644fred,
	&video_h2614rose,
	&video_h2634dave,
	&video_h263_no_name,
	&video_h264_no_name,
	&video_h261_no_name,
	&audio_alaw4fred,
	&audio_ulaw_no_name,
	&audio_ulaw4rose,
	&audio_g722_no_name,
	&audio_g7224dave,
	&audio_alaw_no_name,
	NULL
};

static const struct sdp_topology_stream *top_adave_ulaw_afred_g722_arose_alaw_vdave_h263_vfred_h264_vrose_h261[] = {
	&audio_g7224dave,
	&audio_ulaw_no_name,
	&audio_alaw4fred,
	&audio_g722_no_name,
	&audio_ulaw4rose,
	&audio_alaw_no_name,
	&video_h2634dave,
	&video_h263_no_name,
	&video_h2644fred,
	&video_h264_no_name,
	&video_h2614rose,
	&video_h261_no_name,
	NULL
};

/* Sorting by name and type with no new or deleted streams */
static const struct sdp_update_test mrg_by_name_00 = {
	.initial  = top_adave_alaw_afred_ulaw_arose_g722_vdave_h261_vfred_h263_vrose_h264,
	.update_1 = top_vfred_vrose_vdave_h263_h264_h261_afred_ulaw_arose_g722_adave_alaw,
	.expected = top_adave_ulaw_afred_g722_arose_alaw_vdave_h263_vfred_h264_vrose_h261,
};


static const struct sdp_topology_stream *top_adave_g723_h261[] = {
	&audio_g7224dave,
	&audio_g723_no_name,
	&video_h261_no_name,
	NULL
};

/* Sorting by name and type adding names to streams */
static const struct sdp_update_test mrg_by_name_01 = {
	.initial  = top_ulaw_alaw_h264,
	.update_1 = top_adave_g723_h261,
	.expected = top_adave_g723_h261,
};


/* Sorting by name and type removing names from streams */
static const struct sdp_update_test mrg_by_name_02 = {
	.initial  = top_adave_g723_h261,
	.update_1 = top_ulaw_alaw_h264,
	.expected = top_ulaw_alaw_h264,
};


static const struct sdp_update_test *sdp_update_cases[] = {
	/* Merging by type */
	/* 00 */ &mrg_by_type_00,
	/* 01 */ &mrg_by_type_01,
	/* 02 */ &mrg_by_type_02,
	/* 03 */ &mrg_by_type_03,
	/* 04 */ &mrg_by_type_04,
	/* 05 */ &mrg_by_type_05,
	/* 06 */ &mrg_by_type_06,
	/* 07 */ &mrg_by_type_07,
	/* 08 */ &mrg_by_type_08,
	/* 09 */ &mrg_by_type_09,
	/* 10 */ &mrg_by_type_10,
	/* 11 */ &mrg_by_type_11,

	/* Merging by name and type */
	/* 12 */ &mrg_by_name_00,
	/* 13 */ &mrg_by_name_01,
	/* 14 */ &mrg_by_name_02,
};

AST_TEST_DEFINE(sdp_update_topology)
{
	enum ast_test_result_state res;
	unsigned int idx;
	int status;
	struct ast_sdp_options *options;
	struct ast_stream_topology *topology;
	struct ast_sdp_state *test_state = NULL;

	static const struct sdp_format sdp_formats[] = {
		{ AST_MEDIA_TYPE_AUDIO, "ulaw,alaw,g722,g723" },
		{ AST_MEDIA_TYPE_VIDEO, "h261,h263,h264,vp8" },
		{ AST_MEDIA_TYPE_IMAGE, "t38" },
	};

	switch(cmd) {
	case TEST_INIT:
		info->name = "sdp_update_topology";
		info->category = "/main/sdp/";
		info->summary = "Merge topology updates from the system";
		info->description =
			"1) Create a SDP state with an optional initial topology.\n"
			"2) Update the initial topology with one or two new topologies.\n"
			"3) Get the SDP offer to merge the updates into the initial topology.\n"
			"4) Check that the offered topology matches the expected topology.\n"
			"5) Repeat these steps for each test case defined.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	res = AST_TEST_FAIL;
	for (idx = 0; idx < ARRAY_LEN(sdp_update_cases); ++idx) {
		ast_test_status_update(test, "Starting update case %d\n", idx);

		/* Create a SDP state with an optional initial topology. */
		options = sdp_options_common();
		if (!options) {
			ast_test_status_update(test, "Failed to allocate SDP options\n");
			goto end;
		}
		if (sdp_update_cases[idx]->max_streams) {
			ast_sdp_options_set_max_streams(options, sdp_update_cases[idx]->max_streams);
		}
		if (build_sdp_option_formats(options, ARRAY_LEN(sdp_formats), sdp_formats)) {
			ast_test_status_update(test, "Failed to setup SDP options new stream formats\n");
			goto end;
		}
		if (sdp_update_cases[idx]->initial) {
			topology = build_update_topology(sdp_update_cases[idx]->initial);
			if (!topology) {
				ast_test_status_update(test, "Failed to build initial SDP state topology\n");
				goto end;
			}
		} else {
			topology = NULL;
		}
		test_state = ast_sdp_state_alloc(topology, options);
		ast_stream_topology_free(topology);
		if (!test_state) {
			ast_test_status_update(test, "Failed to build SDP state\n");
			goto end;
		}

		/* Update the initial topology with one or two new topologies. */
		topology = build_update_topology(sdp_update_cases[idx]->update_1);
		if (!topology) {
			ast_test_status_update(test, "Failed to build first update SDP state topology\n");
			goto end;
		}
		status = ast_sdp_state_update_local_topology(test_state, topology);
		ast_stream_topology_free(topology);
		if (status) {
			ast_test_status_update(test, "Failed to update first update SDP state topology\n");
			goto end;
		}
		if (sdp_update_cases[idx]->update_2) {
			topology = build_update_topology(sdp_update_cases[idx]->update_2);
			if (!topology) {
				ast_test_status_update(test, "Failed to build second update SDP state topology\n");
				goto end;
			}
			status = ast_sdp_state_update_local_topology(test_state, topology);
			ast_stream_topology_free(topology);
			if (status) {
				ast_test_status_update(test, "Failed to update second update SDP state topology\n");
				goto end;
			}
		}

		/* Get the SDP offer to merge the updates into the initial topology. */
		if (!ast_sdp_state_get_local_sdp(test_state)) {
			ast_test_status_update(test, "Failed to create offer SDP\n");
			goto end;
		}

		/* Check that the offered topology matches the expected topology. */
		topology = build_update_topology(sdp_update_cases[idx]->expected);
		if (!topology) {
			ast_test_status_update(test, "Failed to build expected topology\n");
			goto end;
		}
		status = cmp_update_topology(test, topology,
			ast_sdp_state_get_local_topology(test_state));
		ast_stream_topology_free(topology);
		if (status) {
			ast_test_status_update(test, "Failed to match expected topology\n");
			goto end;
		}

		/* Repeat for each test case defined. */
		ast_sdp_state_free(test_state);
		test_state = NULL;
	}
	res = AST_TEST_PASS;

end:
	ast_sdp_state_free(test_state);
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(invalid_rtpmap);
	AST_TEST_UNREGISTER(rtpmap);
	AST_TEST_UNREGISTER(find_attr);
	AST_TEST_UNREGISTER(topology_to_sdp);
	AST_TEST_UNREGISTER(sdp_to_topology);
	AST_TEST_UNREGISTER(sdp_negotiation_initial);
	AST_TEST_UNREGISTER(sdp_negotiation_type_change);
	AST_TEST_UNREGISTER(sdp_negotiation_decline_incompatible);
	AST_TEST_UNREGISTER(sdp_negotiation_decline_max_streams);
	AST_TEST_UNREGISTER(sdp_negotiation_not_acceptable);
	AST_TEST_UNREGISTER(sdp_ssrc_attributes);
	AST_TEST_UNREGISTER(sdp_update_topology);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(invalid_rtpmap);
	AST_TEST_REGISTER(rtpmap);
	AST_TEST_REGISTER(find_attr);
	AST_TEST_REGISTER(topology_to_sdp);
	AST_TEST_REGISTER(sdp_to_topology);
	AST_TEST_REGISTER(sdp_negotiation_initial);
	AST_TEST_REGISTER(sdp_negotiation_type_change);
	AST_TEST_REGISTER(sdp_negotiation_decline_incompatible);
	AST_TEST_REGISTER(sdp_negotiation_decline_max_streams);
	AST_TEST_REGISTER(sdp_negotiation_not_acceptable);
	AST_TEST_REGISTER(sdp_ssrc_attributes);
	AST_TEST_REGISTER(sdp_update_topology);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "SDP tests");
