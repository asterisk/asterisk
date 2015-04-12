/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Matt Jordan
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief Unit tests for jitterbuf.c
 *
 * \author\verbatim Matt Jordan <mjordan@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "jitterbuf.h"

#define DEFAULT_MAX_JITTERBUFFER 1000
#define DEFAULT_RESYNCH_THRESHOLD 1000
#define DEFAULT_MAX_CONTIG_INTERP 10
#define DEFAULT_TARGET_EXTRA -1
#define DEFAULT_CODEC_INTERP_LEN 20

/*! \internal
 * Test two numeric (long int) values.  Failure automatically attempts
 * to jump to a cleanup tag
 */
#define JB_NUMERIC_TEST(attribute, expected) do { \
	if ((attribute) != (expected)) { \
		ast_test_status_update(test, #attribute ": expected [%ld]; actual [%ld]\n", (long int)(expected), (attribute)); \
		goto cleanup; \
	} \
} while (0)

/*! \internal
 * Print out as debug the frame related contents of a jb_info object
 */
#define JB_INFO_PRINT_FRAME_DEBUG(jbinfo) do { \
	ast_debug(1, "JitterBuffer Frame Info:\n" \
		"\tFrames In: %ld\n\tFrames Out: %ld\n" \
		"\tDropped Frames: %ld\n\tLate Frames: %ld\n" \
		"\tLost Frames: %ld\n\tOut of Order Frames: %ld\n" \
		"\tCurrent Frame: %ld\n", jbinfo.frames_in, jbinfo.frames_out, \
		jbinfo.frames_dropped, jbinfo.frames_late, jbinfo.frames_lost, \
		jbinfo.frames_ooo, jbinfo.frames_cur); \
} while (0)

/*! \internal
 * This macro installs the error, warning, and debug functions for a test.  It is
 * expected that at the end of a test, the functions are removed.
 * Note that the debug statement is in here merely to aid in tracing in a log where
 * the jitter buffer debug output begins.
 */
#define JB_TEST_BEGIN(test_name) do { \
	jb_setoutput(test_jb_error_output, test_jb_warn_output, test_jb_debug_output); \
	ast_debug(1, "Starting %s\n", test_name); \
} while (0)

/*! \internal
 * Uninstall the error, warning, and debug functions from a test
 */
#define JB_TEST_END do { \
	jb_setoutput(NULL, NULL, NULL); \
} while (0)

static const char *jitter_buffer_return_codes[] = {
	"JB_OK",            /* 0 */
	"JB_EMPTY",         /* 1 */
	"JB_NOFRAME",       /* 2 */
	"JB_INTERP",        /* 3 */
	"JB_DROP",          /* 4 */
	"JB_SCHED"          /* 5 */
};

/*!
 * \internal
 * \brief Make a default jitter buffer configuration
 */
static void test_jb_populate_config(struct jb_conf *jbconf)
{
	if (!jbconf) {
		return;
	}

	jbconf->max_jitterbuf = DEFAULT_MAX_JITTERBUFFER;
	jbconf->resync_threshold = DEFAULT_RESYNCH_THRESHOLD;
	jbconf->max_contig_interp = DEFAULT_MAX_CONTIG_INTERP;
	jbconf->target_extra = 0;
}

/*!
 * \internal
 * \brief Debug callback function for the jitter buffer's jb_dbg function
 */
static void __attribute__((format(printf, 1, 2))) test_jb_debug_output(const char *fmt, ...)
{
	va_list args;
	char buf[1024];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	ast_debug(1, "%s", buf);
}

/*!
 * \internal
 * \brief Warning callback function for the jitter buffer's jb_warn function
 */
static void __attribute__((format(printf, 1, 2))) test_jb_warn_output(const char *fmt, ...)
{
	va_list args;
	char buf[1024];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	ast_log(AST_LOG_WARNING, "%s", buf);
}

/*!
 * \internal
 * \brief Error callback function for the jitter buffer's jb_err function
 */
static void __attribute__((format(printf, 1, 2))) test_jb_error_output(const char *fmt, ...)
{
	va_list args;
	char buf[1024];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	ast_log(AST_LOG_ERROR, "%s", buf);
}

/*!
 * \internal
 * \brief Insert frames into the jitter buffer for the nominal tests
 */
static int test_jb_nominal_frame_insertion(struct ast_test *test, struct jitterbuf *jb, enum jb_frame_type frame_type)
{
	int i = 0, ret = 0;

	for (i = 0; i < 40; i++) {
		if (jb_put(jb, NULL, frame_type, 20, i * 20, i * 20 + 5) == JB_DROP) {
			ast_test_status_update(test, "Jitter buffer dropped packet %d\n", i);
			ret = 1;
			break;
		}
	}

	return ret;
}

AST_TEST_DEFINE(jitterbuffer_nominal_voice_frames)
{
	enum ast_test_result_state result = AST_TEST_FAIL;
	struct jitterbuf *jb = NULL;
	struct jb_frame frame;
	struct jb_conf jbconf;
	struct jb_info jbinfo;
	int i = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "jitterbuffer_nominal_voice_frames";
		info->category = "/main/jitterbuf/";
		info->summary = "Nominal operation of jitter buffer with audio data";
		info->description =
			"Tests the nominal case of putting audio data into a jitter buffer, "
			"retrieving the frames, and querying for the next frame";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	JB_TEST_BEGIN("jitterbuffer_nominal_voice_frames");

	if (!(jb = jb_new())) {
		ast_test_status_update(test, "Failed to allocate memory for jitterbuffer\n");
		goto cleanup;
	}

	test_jb_populate_config(&jbconf);
	if (jb_setconf(jb, &jbconf) != JB_OK) {
		ast_test_status_update(test, "Failed to set jitterbuffer configuration\n");
		goto cleanup;
	}

	if (test_jb_nominal_frame_insertion(test, jb, JB_TYPE_VOICE)) {
		goto cleanup;
	}

	for (i = 0; i < 40; i++) {
		enum jb_return_code ret;
		/* We should have a frame for each point in time */
		if ((ret = jb_get(jb, &frame, i * 20 + 5, DEFAULT_CODEC_INTERP_LEN)) != JB_OK) {
			ast_test_status_update(test,
				"Unexpected jitter buffer return code [%s] when retrieving frame %d\n",
				jitter_buffer_return_codes[ret], i);
			goto cleanup;
		}
		JB_NUMERIC_TEST(frame.ms, 20);
		JB_NUMERIC_TEST(frame.ts, i * 20 - jb->info.resync_offset);
		JB_NUMERIC_TEST(jb_next(jb), (i + 1) * 20 + 5);
	}

	result = AST_TEST_PASS;

	if (jb_getinfo(jb, &jbinfo) != JB_OK) {
		ast_test_status_update(test, "Failed to get jitterbuffer information\n");
		goto cleanup;
	}
	JB_INFO_PRINT_FRAME_DEBUG(jbinfo);
	JB_NUMERIC_TEST(jbinfo.frames_dropped, 0);
	JB_NUMERIC_TEST(jbinfo.frames_in, 40);
	JB_NUMERIC_TEST(jbinfo.frames_out, 40);
	JB_NUMERIC_TEST(jbinfo.frames_late, 0);
	JB_NUMERIC_TEST(jbinfo.frames_lost, 0);
	JB_NUMERIC_TEST(jbinfo.frames_ooo, 0);

cleanup:
	if (jb) {
		/* No need to do anything - this will put all frames on the 'free' list,
		 * so jb_destroy will dispose of them */
		while (jb_getall(jb, &frame) == JB_OK) { }
		jb_destroy(jb);
	}

	JB_TEST_END;

	return result;
}

AST_TEST_DEFINE(jitterbuffer_nominal_control_frames)
{
	enum ast_test_result_state result = AST_TEST_FAIL;
	struct jitterbuf *jb = NULL;
	struct jb_frame frame;
	struct jb_conf jbconf;
	struct jb_info jbinfo;
	int i = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "jitterbuffer_nominal_control_frames";
		info->category = "/main/jitterbuf/";
		info->summary = "Nominal operation of jitter buffer with control frames";
		info->description =
			"Tests the nominal case of putting control frames into a jitter buffer, "
			"retrieving the frames, and querying for the next frame";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	JB_TEST_BEGIN("jitterbuffer_nominal_control_frames");

	if (!(jb = jb_new())) {
		ast_test_status_update(test, "Failed to allocate memory for jitterbuffer\n");
		goto cleanup;
	}

	test_jb_populate_config(&jbconf);
	if (jb_setconf(jb, &jbconf) != JB_OK) {
		ast_test_status_update(test, "Failed to set jitterbuffer configuration\n");
		goto cleanup;
	}

	if (test_jb_nominal_frame_insertion(test, jb, JB_TYPE_CONTROL)) {
		goto cleanup;
	}

	for (i = 0; i < 40; i++) {
		enum jb_return_code ret;
		/* We should have a frame for each point in time */
		if ((ret = jb_get(jb, &frame, i * 20 + 5, DEFAULT_CODEC_INTERP_LEN)) != JB_OK) {
			ast_test_status_update(test,
				"Unexpected jitter buffer return code [%s] when retrieving frame %d\n",
				jitter_buffer_return_codes[ret], i);
			goto cleanup;
		}
		JB_NUMERIC_TEST(frame.ms, 20);
		JB_NUMERIC_TEST(frame.ts, i * 20 - jb->info.resync_offset);
	}

	if (jb_getinfo(jb, &jbinfo) != JB_OK) {
		ast_test_status_update(test, "Failed to get jitterbuffer information\n");
		goto cleanup;
	}
	JB_INFO_PRINT_FRAME_DEBUG(jbinfo);
	JB_NUMERIC_TEST(jbinfo.frames_dropped, 0);
	JB_NUMERIC_TEST(jbinfo.frames_in, 40);
	JB_NUMERIC_TEST(jbinfo.frames_out, 40);
	JB_NUMERIC_TEST(jbinfo.frames_late, 0);
	JB_NUMERIC_TEST(jbinfo.frames_lost, 0);
	JB_NUMERIC_TEST(jbinfo.frames_ooo, 0);

	result = AST_TEST_PASS;

cleanup:
	if (jb) {
		/* No need to do anything - this will put all frames on the 'free' list,
		 * so jb_destroy will dispose of them */
		while (jb_getall(jb, &frame) == JB_OK) { }
		jb_destroy(jb);
	}

	JB_TEST_END;

	return result;
}

/*!
 * \internal
 * \brief Insert frames into the jitter buffer for the out of order tests
 */
static int test_jb_out_of_order_frame_insertion(struct ast_test *test, struct jitterbuf *jb, enum jb_frame_type frame_type)
{
	int i = 0, ret = 0;

	for (i = 0; i < 40; i++) {
		if (i % 4 == 0) {
			/* Add the next frame */
			if (jb_put(jb, NULL, frame_type, 20, (i + 1) * 20, (i + 1) * 20 + 5) == JB_DROP) {
				ast_test_status_update(test, "Jitter buffer dropped packet %d\n", (i+1));
				ret = 1;
				break;
			}
			/* Add the current frame */
			if (jb_put(jb, NULL, frame_type, 20, i * 20, i * 20 + 5) == JB_DROP) {
				ast_test_status_update(test, "Jitter buffer dropped packet %d\n", i);
				ret = 1;
				break;
			}
			i++;
		} else {
			if (jb_put(jb, NULL, frame_type, 20, i * 20, i * 20 + 5) == JB_DROP) {
				ast_test_status_update(test, "Jitter buffer dropped packet %d\n", i);
				ret = 1;
				break;
			}
		}
	}

	return ret;
}

AST_TEST_DEFINE(jitterbuffer_out_of_order_voice)
{
	enum ast_test_result_state result = AST_TEST_FAIL;
	struct jitterbuf *jb = NULL;
	struct jb_frame frame;
	struct jb_info jbinfo;
	struct jb_conf jbconf;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "jitterbuffer_out_of_order_voice";
		info->category = "/main/jitterbuf/";
		info->summary = "Tests sending out of order audio frames to a jitter buffer";
		info->description =
			"Every 5th frame sent to a jitter buffer is reversed with the previous "
			"frame.  The expected result is to have a jitter buffer with the frames "
			"in order, while a total of 10 frames should be recorded as having been "
			"received out of order.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	JB_TEST_BEGIN("jitterbuffer_out_of_order_voice");

	if (!(jb = jb_new())) {
		ast_test_status_update(test, "Failed to allocate memory for jitterbuffer\n");
		goto cleanup;
	}

	test_jb_populate_config(&jbconf);
	if (jb_setconf(jb, &jbconf) != JB_OK) {
		ast_test_status_update(test, "Failed to set jitterbuffer configuration\n");
		goto cleanup;
	}

	if (test_jb_out_of_order_frame_insertion(test, jb, JB_TYPE_VOICE)) {
		goto cleanup;
	}

	for (i = 0; i < 40; i++) {
		enum jb_return_code ret;
		/* We should have a frame for each point in time */
		if ((ret = jb_get(jb, &frame, i * 20 + 5, DEFAULT_CODEC_INTERP_LEN)) != JB_OK) {
			ast_test_status_update(test,
				"Unexpected jitter buffer return code [%s] when retrieving frame %d\n",
				jitter_buffer_return_codes[ret], i);
			goto cleanup;
		}
		JB_NUMERIC_TEST(frame.ms, 20);
		JB_NUMERIC_TEST(frame.ts, i * 20 - jb->info.resync_offset);
	}

	if (jb_getinfo(jb, &jbinfo) != JB_OK) {
		ast_test_status_update(test, "Failed to get jitterbuffer information\n");
		goto cleanup;
	}
	JB_INFO_PRINT_FRAME_DEBUG(jbinfo);
	JB_NUMERIC_TEST(jbinfo.frames_dropped, 0);
	JB_NUMERIC_TEST(jbinfo.frames_in, 40);
	JB_NUMERIC_TEST(jbinfo.frames_out, 40);
	JB_NUMERIC_TEST(jbinfo.frames_late, 0);
	JB_NUMERIC_TEST(jbinfo.frames_lost, 0);
	JB_NUMERIC_TEST(jbinfo.frames_ooo, 10);

	result = AST_TEST_PASS;

cleanup:
	if (jb) {
		/* No need to do anything - this will put all frames on the 'free' list,
		 * so jb_destroy will dispose of them */
		while (jb_getall(jb, &frame) == JB_OK) { }
		jb_destroy(jb);
	}

	JB_TEST_END;

	return result;
}

AST_TEST_DEFINE(jitterbuffer_out_of_order_control)
{
	enum ast_test_result_state result = AST_TEST_FAIL;
	struct jitterbuf *jb = NULL;
	struct jb_frame frame;
	struct jb_info jbinfo;
	struct jb_conf jbconf;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "jitterbuffer_out_of_order_voice";
		info->category = "/main/jitterbuf/";
		info->summary = "Tests sending out of order audio frames to a jitter buffer";
		info->description =
			"Every 5th frame sent to a jitter buffer is reversed with the previous "
			"frame.  The expected result is to have a jitter buffer with the frames "
			"in order, while a total of 10 frames should be recorded as having been "
			"received out of order.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	JB_TEST_BEGIN("jitterbuffer_out_of_order_control");

	if (!(jb = jb_new())) {
		ast_test_status_update(test, "Failed to allocate memory for jitterbuffer\n");
		goto cleanup;
	}

	test_jb_populate_config(&jbconf);
	if (jb_setconf(jb, &jbconf) != JB_OK) {
		ast_test_status_update(test, "Failed to set jitterbuffer configuration\n");
		goto cleanup;
	}

	if (test_jb_out_of_order_frame_insertion(test, jb, JB_TYPE_CONTROL)) {
		goto cleanup;
	}

	for (i = 0; i < 40; i++) {
		enum jb_return_code ret;
		/* We should have a frame for each point in time */
		if ((ret = jb_get(jb, &frame, i * 20 + 5, DEFAULT_CODEC_INTERP_LEN)) != JB_OK) {
			ast_test_status_update(test,
				"Unexpected jitter buffer return code [%s] when retrieving frame %d\n",
				jitter_buffer_return_codes[ret], i);
			goto cleanup;
		}
		JB_NUMERIC_TEST(frame.ms, 20);
		JB_NUMERIC_TEST(frame.ts, i * 20 - jb->info.resync_offset);
	}

	if (jb_getinfo(jb, &jbinfo) != JB_OK) {
		ast_test_status_update(test, "Failed to get jitterbuffer information\n");
		goto cleanup;
	}
	JB_INFO_PRINT_FRAME_DEBUG(jbinfo);
	JB_NUMERIC_TEST(jbinfo.frames_dropped, 0);
	JB_NUMERIC_TEST(jbinfo.frames_in, 40);
	JB_NUMERIC_TEST(jbinfo.frames_out, 40);
	JB_NUMERIC_TEST(jbinfo.frames_late, 0);
	JB_NUMERIC_TEST(jbinfo.frames_lost, 0);
	JB_NUMERIC_TEST(jbinfo.frames_ooo, 10);

	result = AST_TEST_PASS;

cleanup:
	if (jb) {
		/* No need to do anything - this will put all frames on the 'free' list,
		 * so jb_destroy will dispose of them */
		while (jb_getall(jb, &frame) == JB_OK) { }
		jb_destroy(jb);
	}

	JB_TEST_END;

	return result;
}

/*!
 * \internal
 * \brief Insert frames into the jitter buffer for the lost frame tests
 */
static int test_jb_lost_frame_insertion(struct ast_test *test, struct jitterbuf *jb, enum jb_frame_type frame_type)
{
	int i = 0, ret = 0;

	for (i = 0; i < 40; i++) {
		if (i % 5 == 0) {
			i++;
		}
		if (jb_put(jb, NULL, frame_type, 20, i * 20, i * 20 + 5) == JB_DROP) {
			ast_test_status_update(test, "Jitter buffer dropped packet %d\n", i);
			ret = 1;
			break;
		}
	}

	return ret;
}

AST_TEST_DEFINE(jitterbuffer_lost_voice)
{
	enum ast_test_result_state result = AST_TEST_FAIL;
	struct jitterbuf *jb = NULL;
	struct jb_frame frame;
	struct jb_conf jbconf;
	struct jb_info jbinfo;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "jitterbuffer_lost_voice";
		info->category = "/main/jitterbuf/";
		info->summary = "Tests missing frames in the jitterbuffer";
		info->description =
			"Every 5th frame that would be sent to a jitter buffer is instead"
			"dropped.  When reading data from the jitter buffer, the jitter buffer"
			"should interpolate the voice frame.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	JB_TEST_BEGIN("jitterbuffer_lost_voice");

	if (!(jb = jb_new())) {
		ast_test_status_update(test, "Failed to allocate memory for jitterbuffer\n");
		goto cleanup;
	}

	test_jb_populate_config(&jbconf);
	if (jb_setconf(jb, &jbconf) != JB_OK) {
		ast_test_status_update(test, "Failed to set jitterbuffer configuration\n");
		goto cleanup;
	}

	if (test_jb_lost_frame_insertion(test, jb, JB_TYPE_VOICE)) {
		goto cleanup;
	}

	for (i = 0; i < 40; i++) {
		enum jb_return_code ret;
		if ((ret = jb_get(jb, &frame, i * 20 + 5, DEFAULT_CODEC_INTERP_LEN)) != JB_OK) {
			/* If we didn't get an OK, make sure that it was an expected lost frame */
			if (!((ret == JB_INTERP && i % 5 == 0) || (ret == JB_NOFRAME && i == 0))) {
				ast_test_status_update(test,
					"Unexpected jitter buffer return code [%s] when retrieving frame %d\n",
					jitter_buffer_return_codes[ret], i);
				goto cleanup;
			}
		} else {
			JB_NUMERIC_TEST(frame.ms, 20);
			JB_NUMERIC_TEST(frame.ts, i * 20 - jb->info.resync_offset);
		}
	}

	if (jb_getinfo(jb, &jbinfo) != JB_OK) {
		ast_test_status_update(test, "Failed to get jitterbuffer information\n");
		goto cleanup;
	}
	JB_INFO_PRINT_FRAME_DEBUG(jbinfo);
	/* Note: The first frame (at i = 0) never got added, so nothing existed at that point.
	 * Its neither dropped nor lost.
	 */
	JB_NUMERIC_TEST(jbinfo.frames_ooo, 0);
	JB_NUMERIC_TEST(jbinfo.frames_late, 0);
	JB_NUMERIC_TEST(jbinfo.frames_lost, 7);
	JB_NUMERIC_TEST(jbinfo.frames_in, 32);
	JB_NUMERIC_TEST(jbinfo.frames_out, 32);
	JB_NUMERIC_TEST(jbinfo.frames_dropped, 0);

	result = AST_TEST_PASS;

cleanup:
	if (jb) {
		/* No need to do anything - this will put all frames on the 'free' list,
		 * so jb_destroy will dispose of them */
		while (jb_getall(jb, &frame) == JB_OK) { }
		jb_destroy(jb);
	}

	JB_TEST_END;

	return result;
}

AST_TEST_DEFINE(jitterbuffer_lost_control)
{
	enum ast_test_result_state result = AST_TEST_FAIL;
	struct jitterbuf *jb = NULL;
	struct jb_frame frame;
	struct jb_conf jbconf;
	struct jb_info jbinfo;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "jitterbuffer_lost_control";
		info->category = "/main/jitterbuf/";
		info->summary = "Tests missing frames in the jitterbuffer";
		info->description =
			"Every 5th frame that would be sent to a jitter buffer is instead"
			"dropped.  When reading data from the jitter buffer, the jitter buffer"
			"simply reports that no frame exists for that time slot";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	JB_TEST_BEGIN("jitterbuffer_lost_control");

	if (!(jb = jb_new())) {
		ast_test_status_update(test, "Failed to allocate memory for jitterbuffer\n");
		goto cleanup;
	}

	test_jb_populate_config(&jbconf);
	if (jb_setconf(jb, &jbconf) != JB_OK) {
		ast_test_status_update(test, "Failed to set jitterbuffer configuration\n");
		goto cleanup;
	}

	if (test_jb_lost_frame_insertion(test, jb, JB_TYPE_CONTROL)) {
		goto cleanup;
	}

	for (i = 0; i < 40; i++) {
		enum jb_return_code ret;
		if ((ret = jb_get(jb, &frame, i * 20 + 5, DEFAULT_CODEC_INTERP_LEN)) != JB_OK) {
			/* If we didn't get an OK, make sure that it was an expected lost frame */
			if (!(ret == JB_NOFRAME && i % 5 == 0)) {
				ast_test_status_update(test,
					"Unexpected jitter buffer return code [%s] when retrieving frame %d\n",
					jitter_buffer_return_codes[ret], i);
				goto cleanup;
			}
		} else {
			JB_NUMERIC_TEST(frame.ms, 20);
			JB_NUMERIC_TEST(frame.ts, i * 20 - jb->info.resync_offset);
		}
	}

	if (jb_getinfo(jb, &jbinfo) != JB_OK) {
		ast_test_status_update(test, "Failed to get jitterbuffer information\n");
		goto cleanup;
	}
	JB_INFO_PRINT_FRAME_DEBUG(jbinfo);
	/* Note: The first frame (at i = 0) never got added, so nothing existed at that point.
	 * Its neither dropped nor lost.
	 */
	JB_NUMERIC_TEST(jbinfo.frames_ooo, 0);
	JB_NUMERIC_TEST(jbinfo.frames_late, 0);
	JB_NUMERIC_TEST(jbinfo.frames_lost, 0);
	JB_NUMERIC_TEST(jbinfo.frames_in, 32);
	JB_NUMERIC_TEST(jbinfo.frames_out, 32);
	JB_NUMERIC_TEST(jbinfo.frames_dropped, 0);

	result = AST_TEST_PASS;

cleanup:
	if (jb) {
		/* No need to do anything - this will put all frames on the 'free' list,
		 * so jb_destroy will dispose of them */
		while (jb_getall(jb, &frame) == JB_OK) { }
		jb_destroy(jb);
	}

	JB_TEST_END;

	return result;
}

/*!
 * \internal
 * \brief Insert frames into the jitter buffer for the late frame tests
 */
static int test_jb_late_frame_insertion(struct ast_test *test, struct jitterbuf *jb, enum jb_frame_type frame_type)
{
	int i = 0, ret = 0;

	for (i = 0; i < 40; i++) {
		if (i % 5 == 0) {
			/* Add 5th frame */
			if (jb_put(jb, NULL, frame_type, 20, i * 20, i * 20 + 20) == JB_DROP) {
				ast_test_status_update(test, "Jitter buffer dropped packet %d\n", (i+1));
				ret = 1;
				break;
			}
		} else {
			if (jb_put(jb, NULL, frame_type, 20, i * 20, i * 20 + 5) == JB_DROP) {
				ast_test_status_update(test, "Jitter buffer dropped packet %d\n", i);
				ret = 1;
				break;
			}
		}
	}

	return ret;
}

AST_TEST_DEFINE(jitterbuffer_late_voice)
{
	enum ast_test_result_state result = AST_TEST_FAIL;
	struct jitterbuf *jb = NULL;
	struct jb_frame frame;
	struct jb_info jbinfo;
	struct jb_conf jbconf;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "jitterbuffer_late_voice";
		info->category = "/main/jitterbuf/";
		info->summary = "Tests sending frames to a jitter buffer that arrive late";
		info->description =
			"Every 5th frame sent to a jitter buffer arrives late, but still in "
			"order with respect to the previous and next packet";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	JB_TEST_BEGIN("jitterbuffer_late_voice");

	if (!(jb = jb_new())) {
		ast_test_status_update(test, "Failed to allocate memory for jitterbuffer\n");
		goto cleanup;
	}

	test_jb_populate_config(&jbconf);
	if (jb_setconf(jb, &jbconf) != JB_OK) {
		ast_test_status_update(test, "Failed to set jitterbuffer configuration\n");
		goto cleanup;
	}

	if (test_jb_late_frame_insertion(test, jb, JB_TYPE_VOICE)) {
		goto cleanup;
	}

	for (i = 0; i < 40; i++) {
		enum jb_return_code ret;
		/* We should have a frame for each point in time */
		if ((ret = jb_get(jb, &frame, i * 20 + 5, DEFAULT_CODEC_INTERP_LEN)) != JB_OK) {
			ast_test_status_update(test,
				"Unexpected jitter buffer return code [%s] when retrieving frame %d\n",
				jitter_buffer_return_codes[ret], i);
			goto cleanup;
		}
		JB_NUMERIC_TEST(frame.ms, 20);
		JB_NUMERIC_TEST(frame.ts, i * 20 - jb->info.resync_offset);
	}

	if (jb_getinfo(jb, &jbinfo) != JB_OK) {
		ast_test_status_update(test, "Failed to get jitterbuffer information\n");
		goto cleanup;
	}
	JB_INFO_PRINT_FRAME_DEBUG(jbinfo);
	JB_NUMERIC_TEST(jbinfo.frames_ooo, 0);
	JB_NUMERIC_TEST(jbinfo.frames_late, 0);
	JB_NUMERIC_TEST(jbinfo.frames_lost, 0);
	JB_NUMERIC_TEST(jbinfo.frames_in, 40);
	JB_NUMERIC_TEST(jbinfo.frames_out, 40);
	JB_NUMERIC_TEST(jbinfo.frames_dropped, 0);

	result = AST_TEST_PASS;

cleanup:
	if (jb) {
		/* No need to do anything - this will put all frames on the 'free' list,
		 * so jb_destroy will dispose of them */
		while (jb_getall(jb, &frame) == JB_OK) { }
		jb_destroy(jb);
	}

	JB_TEST_END;

	return result;
}

AST_TEST_DEFINE(jitterbuffer_late_control)
{
	enum ast_test_result_state result = AST_TEST_FAIL;
	struct jitterbuf *jb = NULL;
	struct jb_frame frame;
	struct jb_info jbinfo;
	struct jb_conf jbconf;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "jitterbuffer_late_control";
		info->category = "/main/jitterbuf/";
		info->summary = "Tests sending frames to a jitter buffer that arrive late";
		info->description =
			"Every 5th frame sent to a jitter buffer arrives late, but still in "
			"order with respect to the previous and next packet";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	JB_TEST_BEGIN("jitterbuffer_late_voice");

	if (!(jb = jb_new())) {
		ast_test_status_update(test, "Failed to allocate memory for jitterbuffer\n");
		goto cleanup;
	}

	test_jb_populate_config(&jbconf);
	if (jb_setconf(jb, &jbconf) != JB_OK) {
		ast_test_status_update(test, "Failed to set jitterbuffer configuration\n");
		goto cleanup;
	}

	if (test_jb_late_frame_insertion(test, jb, JB_TYPE_CONTROL)) {
		goto cleanup;
	}

	for (i = 0; i < 40; i++) {
		enum jb_return_code ret;
		/* We should have a frame for each point in time */
		if ((ret = jb_get(jb, &frame, i * 20 + 5, DEFAULT_CODEC_INTERP_LEN)) != JB_OK) {
			ast_test_status_update(test,
				"Unexpected jitter buffer return code [%s] when retrieving frame %d\n",
				jitter_buffer_return_codes[ret], i);
			goto cleanup;
		}
		JB_NUMERIC_TEST(frame.ms, 20);
		JB_NUMERIC_TEST(frame.ts, i * 20 - jb->info.resync_offset);
	}

	if (jb_getinfo(jb, &jbinfo) != JB_OK) {
		ast_test_status_update(test, "Failed to get jitterbuffer information\n");
		goto cleanup;
	}
	JB_INFO_PRINT_FRAME_DEBUG(jbinfo);
	JB_NUMERIC_TEST(jbinfo.frames_ooo, 0);
	JB_NUMERIC_TEST(jbinfo.frames_late, 0);
	JB_NUMERIC_TEST(jbinfo.frames_lost, 0);
	JB_NUMERIC_TEST(jbinfo.frames_in, 40);
	JB_NUMERIC_TEST(jbinfo.frames_out, 40);
	JB_NUMERIC_TEST(jbinfo.frames_dropped, 0);

	result = AST_TEST_PASS;

cleanup:
	if (jb) {
		/* No need to do anything - this will put all frames on the 'free' list,
		 * so jb_destroy will dispose of them */
		while (jb_getall(jb, &frame) == JB_OK) { }
		jb_destroy(jb);
	}

	JB_TEST_END;

	return result;
}

/*!
 * \internal
 * \brief Insert frames into the jitter buffer for the overflow tests
 */
static void test_jb_overflow_frame_insertion(struct jitterbuf *jb, enum jb_frame_type frame_type)
{
	int i = 0;

	for (i = 0; i < 100; i++) {
		jb_put(jb, NULL, frame_type, 20, i * 20, i * 20 + 5);
	}
}

AST_TEST_DEFINE(jitterbuffer_overflow_voice)
{
	enum ast_test_result_state result = AST_TEST_FAIL;
	struct jitterbuf *jb = NULL;
	struct jb_frame frame;
	struct jb_info jbinfo;
	struct jb_conf jbconf;
	int i = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "jitterbuffer_overflow_voice";
		info->category = "/main/jitterbuf/";
		info->summary = "Tests overfilling a jitter buffer with voice frames";
		info->description = "Tests overfilling a jitter buffer with voice frames";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	JB_TEST_BEGIN("jitterbuffer_overflow_voice");

	if (!(jb = jb_new())) {
		ast_test_status_update(test, "Failed to allocate memory for jitterbuffer\n");
		goto cleanup;
	}

	test_jb_populate_config(&jbconf);
	if (jb_setconf(jb, &jbconf) != JB_OK) {
		ast_test_status_update(test, "Failed to set jitterbuffer configuration\n");
		goto cleanup;
	}

	test_jb_overflow_frame_insertion(jb, JB_TYPE_VOICE);

	while (jb_get(jb, &frame, i * 20 + 5, DEFAULT_CODEC_INTERP_LEN) == JB_OK) {
		JB_NUMERIC_TEST(frame.ms, 20);
		JB_NUMERIC_TEST(frame.ts, i * 20 - jb->info.resync_offset);
		++i;
	}

	if (jb_getinfo(jb, &jbinfo) != JB_OK) {
		ast_test_status_update(test, "Failed to get jitterbuffer information\n");
		goto cleanup;
	}

	JB_INFO_PRINT_FRAME_DEBUG(jbinfo);
	JB_NUMERIC_TEST(jbinfo.frames_dropped, 49);
	JB_NUMERIC_TEST(jbinfo.frames_out, 51);
	JB_NUMERIC_TEST(jbinfo.frames_in, 51);
	JB_NUMERIC_TEST(jbinfo.frames_late, 0);
	/* Note that the last frame will be interpolated */
	JB_NUMERIC_TEST(jbinfo.frames_lost, 1);
	JB_NUMERIC_TEST(jbinfo.frames_ooo, 0);

	result = AST_TEST_PASS;

cleanup:
	if (jb) {
		/* No need to do anything - this will put all frames on the 'free' list,
		 * so jb_destroy will dispose of them */
		while (jb_getall(jb, &frame) == JB_OK) { }
		jb_destroy(jb);
	}

	JB_TEST_END;

	return result;
}

AST_TEST_DEFINE(jitterbuffer_overflow_control)
{
	enum ast_test_result_state result = AST_TEST_FAIL;
	struct jitterbuf *jb = NULL;
	struct jb_frame frame;
	struct jb_info jbinfo;
	struct jb_conf jbconf;
	int i = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "jitterbuffer_overflow_control";
		info->category = "/main/jitterbuf/";
		info->summary = "Tests overfilling a jitter buffer with control frames";
		info->description = "Tests overfilling a jitter buffer with control frames";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	JB_TEST_BEGIN("jitterbuffer_overflow_control");

	if (!(jb = jb_new())) {
		ast_test_status_update(test, "Failed to allocate memory for jitterbuffer\n");
		goto cleanup;
	}

	test_jb_populate_config(&jbconf);
	if (jb_setconf(jb, &jbconf) != JB_OK) {
		ast_test_status_update(test, "Failed to set jitterbuffer configuration\n");
		goto cleanup;
	}

	test_jb_overflow_frame_insertion(jb, JB_TYPE_CONTROL);

	while (jb_get(jb, &frame, i * 20 + 5, DEFAULT_CODEC_INTERP_LEN) == JB_OK) {
		JB_NUMERIC_TEST(frame.ms, 20);
		JB_NUMERIC_TEST(frame.ts, i * 20 - jb->info.resync_offset);
		++i;
	}

	if (jb_getinfo(jb, &jbinfo) != JB_OK) {
		ast_test_status_update(test, "Failed to get jitterbuffer information\n");
		goto cleanup;
	}

	JB_INFO_PRINT_FRAME_DEBUG(jbinfo);
	JB_NUMERIC_TEST(jbinfo.frames_dropped, 49);
	JB_NUMERIC_TEST(jbinfo.frames_out, 51);
	JB_NUMERIC_TEST(jbinfo.frames_in, 51);
	JB_NUMERIC_TEST(jbinfo.frames_late, 0);
	JB_NUMERIC_TEST(jbinfo.frames_lost, 0);
	JB_NUMERIC_TEST(jbinfo.frames_ooo, 0);

	result = AST_TEST_PASS;

cleanup:
	if (jb) {
		/* No need to do anything - this will put all frames on the 'free' list,
		 * so jb_destroy will dispose of them */
		while (jb_getall(jb, &frame) == JB_OK) { }
		jb_destroy(jb);
	}

	JB_TEST_END;

	return result;
}

/*!
 * \internal
 * \brief Insert frames into the jitter buffer for the resynch tests
 */
static void test_jb_resynch_frame_insertion(struct jitterbuf *jb, enum jb_frame_type frame_type)
{
	int i = 0;

	for (i = 0; i < 20; i++) {
		jb_put(jb, NULL, frame_type, 20, i * 20, i * 20 + 5);
	}

	for (i = 20; i < 40; i++) {
		jb_put(jb, NULL, frame_type, 20, i * 20 + 500, i * 20 + 5);
	}
}

AST_TEST_DEFINE(jitterbuffer_resynch_control)
{
	enum ast_test_result_state result = AST_TEST_FAIL;
	struct jitterbuf *jb = NULL;
	struct jb_frame frame;
	struct jb_info jbinfo;
	struct jb_conf jbconf;
	int interpolated_frames = 0;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "jitterbuffer_resynch_control";
		info->category = "/main/jitterbuf/";
		info->summary = "Tests sending control frames that force a resynch";
		info->description = "Control frames are sent to a jitter buffer.  After some "
			"number of frames, the source timestamps jump, forcing a resync of "
			"the jitter buffer.  Since the frames are control, the resync happens "
			"immediately.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	JB_TEST_BEGIN("jitterbuffer_resynch_control");

	if (!(jb = jb_new())) {
		ast_test_status_update(test, "Failed to allocate memory for jitterbuffer\n");
		goto cleanup;
	}

	test_jb_populate_config(&jbconf);
	jbconf.resync_threshold = 200;
	if (jb_setconf(jb, &jbconf) != JB_OK) {
		ast_test_status_update(test, "Failed to set jitterbuffer configuration\n");
		goto cleanup;
	}

	test_jb_resynch_frame_insertion(jb, JB_TYPE_CONTROL);

	for (i = 0; i <= 40; i++) {
		if (jb_get(jb, &frame, i * 20 + 5, DEFAULT_CODEC_INTERP_LEN) == JB_INTERP) {
			++interpolated_frames;
		}
	}

	if (jb_getinfo(jb, &jbinfo) != JB_OK) {
		ast_test_status_update(test, "Failed to get jitterbuffer information\n");
		goto cleanup;
	}
	/* With control frames, a resync happens automatically */
	JB_INFO_PRINT_FRAME_DEBUG(jbinfo);
	JB_NUMERIC_TEST(jbinfo.frames_dropped, 0);
	JB_NUMERIC_TEST(jbinfo.frames_out, 40);
	JB_NUMERIC_TEST(jbinfo.frames_in, 40);
	/* Verify that each of the interpolated frames is counted */
	JB_NUMERIC_TEST(jbinfo.frames_lost, interpolated_frames);
	JB_NUMERIC_TEST(jbinfo.frames_late, 0);
	JB_NUMERIC_TEST(jbinfo.frames_ooo, 0);

	result = AST_TEST_PASS;

cleanup:
	if (jb) {
		/* No need to do anything - this will put all frames on the 'free' list,
		 * so jb_destroy will dispose of them */
		while (jb_getall(jb, &frame) == JB_OK) { }
		jb_destroy(jb);
	}

	JB_TEST_END;

	return result;
}

AST_TEST_DEFINE(jitterbuffer_resynch_voice)
{
	enum ast_test_result_state result = AST_TEST_FAIL;
	struct jitterbuf *jb = NULL;
	struct jb_frame frame;
	struct jb_info jbinfo;
	struct jb_conf jbconf;
	int interpolated_frames = 0;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "jitterbuffer_resynch_voice";
		info->category = "/main/jitterbuf/";
		info->summary = "Tests sending voice frames that force a resynch";
		info->description = "Voice frames are sent to a jitter buffer.  After some "
			"number of frames, the source timestamps jump, forcing a resync of "
			"the jitter buffer.  Since the frames are voice, the resync happens "
			"after observing three packets that break the resync threshold.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	JB_TEST_BEGIN("jitterbuffer_resynch_voice");

	if (!(jb = jb_new())) {
		ast_test_status_update(test, "Failed to allocate memory for jitterbuffer\n");
		goto cleanup;
	}

	test_jb_populate_config(&jbconf);
	jbconf.resync_threshold = 200;
	if (jb_setconf(jb, &jbconf) != JB_OK) {
		ast_test_status_update(test, "Failed to set jitterbuffer configuration\n");
		goto cleanup;
	}

	test_jb_resynch_frame_insertion(jb, JB_TYPE_VOICE);

	for (i = 0; i <= 40; i++) {
		if (jb_get(jb, &frame, i * 20 + 5, DEFAULT_CODEC_INTERP_LEN) == JB_INTERP) {
			++interpolated_frames;
		}
	}

	if (jb_getinfo(jb, &jbinfo) != JB_OK) {
		ast_test_status_update(test, "Failed to get jitterbuffer information\n");
		goto cleanup;
	}
	/* The first three packets before the resync should be dropped */
	JB_INFO_PRINT_FRAME_DEBUG(jbinfo);
	JB_NUMERIC_TEST(jbinfo.frames_dropped, 3);
	JB_NUMERIC_TEST(jbinfo.frames_out, 37);
	JB_NUMERIC_TEST(jbinfo.frames_in, 37);
	/* Verify that each of the interpolated frames is counted */
	JB_NUMERIC_TEST(jbinfo.frames_lost, interpolated_frames);
	JB_NUMERIC_TEST(jbinfo.frames_late, 0);
	JB_NUMERIC_TEST(jbinfo.frames_ooo, 0);


	result = AST_TEST_PASS;

cleanup:
	if (jb) {
		/* No need to do anything - this will put all frames on the 'free' list,
		 * so jb_destroy will dispose of them */
		while (jb_getall(jb, &frame) == JB_OK) { }
		jb_destroy(jb);
	}

	JB_TEST_END;

	return result;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(jitterbuffer_nominal_voice_frames);
	AST_TEST_UNREGISTER(jitterbuffer_nominal_control_frames);
	AST_TEST_UNREGISTER(jitterbuffer_out_of_order_voice);
	AST_TEST_UNREGISTER(jitterbuffer_out_of_order_control);
	AST_TEST_UNREGISTER(jitterbuffer_lost_voice);
	AST_TEST_UNREGISTER(jitterbuffer_lost_control);
	AST_TEST_UNREGISTER(jitterbuffer_late_voice);
	AST_TEST_UNREGISTER(jitterbuffer_late_control);
	AST_TEST_UNREGISTER(jitterbuffer_overflow_voice);
	AST_TEST_UNREGISTER(jitterbuffer_overflow_control);
	AST_TEST_UNREGISTER(jitterbuffer_resynch_voice);
	AST_TEST_UNREGISTER(jitterbuffer_resynch_control);
	return 0;
}

static int load_module(void)
{
	/* Nominal - put / get frames */
	AST_TEST_REGISTER(jitterbuffer_nominal_voice_frames);
	AST_TEST_REGISTER(jitterbuffer_nominal_control_frames);

	/* Out of order frame arrival */
	AST_TEST_REGISTER(jitterbuffer_out_of_order_voice);
	AST_TEST_REGISTER(jitterbuffer_out_of_order_control);

	/* Lost frame arrival */
	AST_TEST_REGISTER(jitterbuffer_lost_voice);
	AST_TEST_REGISTER(jitterbuffer_lost_control);

	/* Late frame arrival */
	AST_TEST_REGISTER(jitterbuffer_late_voice);
	AST_TEST_REGISTER(jitterbuffer_late_control);

	/* Buffer overflow */
	AST_TEST_REGISTER(jitterbuffer_overflow_voice);
	AST_TEST_REGISTER(jitterbuffer_overflow_control);

	/* Buffer resynch */
	AST_TEST_REGISTER(jitterbuffer_resynch_voice);
	AST_TEST_REGISTER(jitterbuffer_resynch_control);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Jitter Buffer Tests");
