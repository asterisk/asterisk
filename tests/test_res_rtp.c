/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2019, Sangoma, Inc.
 *
 * Ben Ford <bford@sangoma.com>
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
 * \brief RTP/RTCP Unit Tests
 *
 * \author Ben Ford <bford@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/data_buffer.h"
#include "asterisk/format_cache.h"

enum test_type {
	TEST_TYPE_NONE = 0,	/* No special setup required */
	TEST_TYPE_NACK,		/* Enable NACK */
	TEST_TYPE_REMB,		/* Enable REMB */
};

static void ast_sched_context_destroy_wrapper(struct ast_sched_context *sched)
{
	if (sched) {
		ast_sched_context_destroy(sched);
	}
}

static int test_init_rtp_instances(struct ast_rtp_instance **instance1,
	struct ast_rtp_instance **instance2, struct ast_sched_context *test_sched,
	enum test_type type)
{
	struct ast_sockaddr addr;

	ast_sockaddr_parse(&addr, "127.0.0.1", 0);

	*instance1 = ast_rtp_instance_new("asterisk", test_sched, &addr, NULL);
	*instance2 = ast_rtp_instance_new("asterisk", test_sched, &addr, NULL);
	if (!instance1 || !instance2) {
		return -1;
	}

	ast_rtp_instance_set_prop(*instance1, AST_RTP_PROPERTY_RTCP, AST_RTP_INSTANCE_RTCP_MUX);
	ast_rtp_instance_set_prop(*instance2, AST_RTP_PROPERTY_RTCP, AST_RTP_INSTANCE_RTCP_MUX);

	if (type == TEST_TYPE_NACK) {
		ast_rtp_instance_set_prop(*instance1, AST_RTP_PROPERTY_RETRANS_RECV, 1);
		ast_rtp_instance_set_prop(*instance1, AST_RTP_PROPERTY_RETRANS_SEND, 1);
		ast_rtp_instance_set_prop(*instance2, AST_RTP_PROPERTY_RETRANS_RECV, 2);
		ast_rtp_instance_set_prop(*instance2, AST_RTP_PROPERTY_RETRANS_SEND, 2);
	} else if (type == TEST_TYPE_REMB) {
		ast_rtp_instance_set_prop(*instance1, AST_RTP_PROPERTY_REMB, 1);
		ast_rtp_instance_set_prop(*instance2, AST_RTP_PROPERTY_REMB, 1);
	}

	ast_rtp_instance_get_local_address(*instance1, &addr);
	ast_rtp_instance_set_remote_address(*instance2, &addr);

	ast_rtp_instance_get_local_address(*instance2, &addr);
	ast_rtp_instance_set_remote_address(*instance1, &addr);

	ast_rtp_instance_reset_test_engine(*instance1);

	ast_rtp_instance_activate(*instance1);
	ast_rtp_instance_activate(*instance2);

	return 0;
}

static void test_write_frames(struct ast_rtp_instance *instance, int seqno, int num)
{
	char data[320] = "";
	struct ast_frame frame_out = {
		.frametype = AST_FRAME_VOICE,
		.subclass.format = ast_format_ulaw,
		.data.ptr = data,
		.datalen = 160,
	};
	int index;

	ast_set_flag(&frame_out, AST_FRFLAG_HAS_SEQUENCE_NUMBER);

	for (index = 0; index < num; index++) {
		frame_out.seqno = seqno + index;
		ast_rtp_instance_write(instance, &frame_out);
	}
}

static void test_read_frames(struct ast_rtp_instance *instance, int num)
{
	struct ast_frame *frame_in;
	int index;

	for (index = 0; index < num; index++) {
		frame_in = ast_rtp_instance_read(instance, 0);
		if (frame_in) {
			ast_frfree(frame_in);
		}
	}
}

static void test_write_and_read_frames(struct ast_rtp_instance *instance1,
	struct ast_rtp_instance *instance2, int seqno, int num)
{
	test_write_frames(instance1, seqno, num);
	test_read_frames(instance2, num);
}

AST_TEST_DEFINE(nack_no_packet_loss)
{
	RAII_VAR(struct ast_rtp_instance *, instance1, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_rtp_instance *, instance2, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_sched_context *, test_sched, NULL, ast_sched_context_destroy_wrapper);

	switch (cmd) {
	case TEST_INIT:
		info->name = "nack_no_packet_loss";
		info->category = "/res/res_rtp/";
		info->summary = "nack no packet loss unit test";
		info->description =
			"Tests sending packets with no packet loss and "
			"validates that the send buffer stores sent packets "
			"and the receive buffer is empty";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	test_sched = ast_sched_context_create();

	if ((test_init_rtp_instances(&instance1, &instance2, test_sched, TEST_TYPE_NACK)) < 0) {
		ast_log(LOG_ERROR, "Failed to initialize test!\n");
		return AST_TEST_FAIL;
	}

	test_write_and_read_frames(instance1, instance2, 1000, 10);

	ast_test_validate(test, ast_rtp_instance_get_send_buffer_count(instance1) == 10,
		"Send buffer did not have the expected count of 10");

	ast_test_validate(test, ast_rtp_instance_get_recv_buffer_count(instance2) == 0,
		"Receive buffer did not have the expected count of 0");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(nack_nominal)
{
	RAII_VAR(struct ast_rtp_instance *, instance1, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_rtp_instance *, instance2, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_sched_context *, test_sched, NULL, ast_sched_context_destroy_wrapper);

	switch (cmd) {
	case TEST_INIT:
		info->name = "nack_nominal";
		info->category = "/res/res_rtp/";
		info->summary = "nack nominal unit test";
		info->description =
			"Tests sending packets with some packet loss and "
			"validates that a NACK request is sent on reaching "
			"the triggering amount of lost packets";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	test_sched = ast_sched_context_create();

	if ((test_init_rtp_instances(&instance1, &instance2, test_sched, TEST_TYPE_NACK)) < 0) {
		ast_log(LOG_ERROR, "Failed to initialize test!\n");
		return AST_TEST_FAIL;
	}

	/* Start normally */
	test_write_and_read_frames(instance1, instance2, 1000, 10);

	/* Set the number of packets to drop when we send them next */
	ast_rtp_instance_drop_packets(instance2, 10);
	test_write_and_read_frames(instance1, instance2, 1010, 10);

	/* Send enough packets to reach the NACK trigger */
	test_write_and_read_frames(instance1, instance2, 1020, ast_rtp_instance_get_recv_buffer_max(instance2) / 2);

	/* This needs to be read as RTCP */
	test_read_frames(instance1, 1);

	/* We should have the missing packets to read now */
	test_read_frames(instance2, 10);

	ast_test_validate(test, ast_rtp_instance_get_recv_buffer_count(instance2) == 0,
		"Receive buffer did not have the expected count of 0");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(nack_overflow)
{
	RAII_VAR(struct ast_rtp_instance *, instance1, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_rtp_instance *, instance2, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_sched_context *, test_sched, NULL, ast_sched_context_destroy_wrapper);
	int max_packets;

	switch (cmd) {
	case TEST_INIT:
		info->name = "nack_overflow";
		info->category = "/res/res_rtp/";
		info->summary = "nack overflow unit test";
		info->description =
			"Tests that when the buffer hits its capacity, we "
			"queue all the packets we currently have stored";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	test_sched = ast_sched_context_create();

	if ((test_init_rtp_instances(&instance1, &instance2, test_sched, TEST_TYPE_NACK)) < 0) {
		ast_log(LOG_ERROR, "Failed to initialize test!\n");
		return AST_TEST_FAIL;
	}

	/* Start normally */
	test_write_and_read_frames(instance1, instance2, 1000, 10);

	/* Send enough packets to fill the buffer */
	max_packets = ast_rtp_instance_get_recv_buffer_max(instance2);
	test_write_and_read_frames(instance1, instance2, 1020, max_packets);

	ast_test_validate(test, ast_rtp_instance_get_recv_buffer_count(instance2) == max_packets,
		"Receive buffer did not have the expected count of max buffer size");

	/* Send the packet that will overflow the buffer */
	test_write_and_read_frames(instance1, instance2, 1020 + max_packets, 1);

	ast_test_validate(test, ast_rtp_instance_get_recv_buffer_count(instance2) == 0,
		"Receive buffer did not have the expected count of 0");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(lost_packet_stats_nominal)
{
	RAII_VAR(struct ast_rtp_instance *, instance1, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_rtp_instance *, instance2, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_sched_context *, test_sched, NULL, ast_sched_context_destroy_wrapper);
	struct ast_rtp_instance_stats stats = { 0, };
	enum ast_rtp_instance_stat stat = AST_RTP_INSTANCE_STAT_ALL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "lost_packet_stats_nominal";
		info->category = "/res/res_rtp/";
		info->summary = "lost packet stats nominal unit test";
		info->description =
			"Tests that when some packets are lost, we calculate that "
			"loss correctly when doing lost packet statistics";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	test_sched = ast_sched_context_create();

	if ((test_init_rtp_instances(&instance1, &instance2, test_sched, TEST_TYPE_NONE)) < 0) {
		ast_log(LOG_ERROR, "Failed to initialize test!\n");
		return AST_TEST_FAIL;
	}

	/* Start normally */
	test_write_and_read_frames(instance1, instance2, 1000, 10);

	/* Send some more packets, but with a gap */
	test_write_and_read_frames(instance1, instance2, 1015, 5);

	/* Send a RR to calculate lost packet statistics. We should be missing 5 packets */
	ast_rtp_instance_queue_report(instance1);
	test_write_frames(instance2, 1000, 1);

	/* Check RTCP stats to see if we got the expected packet loss count */
	ast_rtp_instance_get_stats(instance2, &stats, stat);
	ast_test_validate(test, stats.rxploss == 5 && stats.local_minrxploss == 5 &&
		stats.local_maxrxploss == 5, "Condition of 5 lost packets was not met");

	/* Drop 3 before writing 5 more */
	test_write_and_read_frames(instance1, instance2, 1023, 5);

	ast_rtp_instance_queue_report(instance1);
	test_write_frames(instance2, 1001, 1);
	ast_rtp_instance_get_stats(instance2, &stats, stat);

	/* Should now be missing 8 total packets with a change in min */
	ast_test_validate(test, stats.rxploss == 8 && stats.local_minrxploss == 3 &&
		stats.local_maxrxploss == 5);

	/* Write 5 more with no gaps */
	test_write_and_read_frames(instance1, instance2, 1028, 5);

	ast_rtp_instance_queue_report(instance1);
	test_write_frames(instance2, 1002, 1);
	ast_rtp_instance_get_stats(instance2, &stats, stat);

	/* Should still only be missing 8 total packets */
	ast_test_validate(test, stats.rxploss == 8 && stats.local_minrxploss == 3 &&
		stats.local_maxrxploss == 5);

	/* Now drop 1, write another 5, drop 8, and then write 5 */
	test_write_and_read_frames(instance1, instance2, 1034, 5);
	test_write_and_read_frames(instance1, instance2, 1047, 5);

	ast_rtp_instance_queue_report(instance1);
	test_write_frames(instance2, 1003, 1);
	ast_rtp_instance_get_stats(instance2, &stats, stat);

	/* Now it should be missing 17 total packets, with a change in max */
	ast_test_validate(test, stats.rxploss == 17 && stats.local_minrxploss == 3 &&
		stats.local_maxrxploss == 9);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(remb_nominal)
{
	RAII_VAR(struct ast_rtp_instance *, instance1, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_rtp_instance *, instance2, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_sched_context *, test_sched, NULL, ast_sched_context_destroy_wrapper);
	RAII_VAR(struct ast_frame *, frame_in, NULL, ast_frfree);
	/* Use the structure softmix_remb_collector uses to store information for REMB */
	struct ast_rtp_rtcp_feedback feedback = {
		.fmt = AST_RTP_RTCP_FMT_REMB,
		.remb.br_exp = 0,
		.remb.br_mantissa = 1000,
	};
	struct ast_frame frame_out = {
		.frametype = AST_FRAME_RTCP,
		.subclass.integer = AST_RTP_RTCP_PSFB,
		.data.ptr = &feedback,
		.datalen = sizeof(feedback),
	};
	struct ast_rtp_rtcp_feedback *received_feedback;

	switch (cmd) {
	case TEST_INIT:
		info->name = "remb_nominal";
		info->category = "/res/res_rtp/";
		info->summary = "remb nominal unit test";
		info->description =
			"Tests sending and receiving a REMB packet";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	test_sched = ast_sched_context_create();

	if ((test_init_rtp_instances(&instance1, &instance2, test_sched, TEST_TYPE_REMB)) < 0) {
		ast_log(LOG_ERROR, "Failed to initialize test!\n");
		return AST_TEST_FAIL;
	}

	/* The schedid must be 0 or greater, so let's do that now */
	ast_rtp_instance_set_schedid(instance1, 0);

	ast_rtp_instance_write(instance1, &frame_out);

	/* Verify the high level aspects of the frame */
	frame_in = ast_rtp_instance_read(instance2, 0);
	ast_test_validate(test, frame_in != NULL, "Did not receive a REMB frame");
	ast_test_validate(test, frame_in->frametype == AST_FRAME_RTCP,
		"REMB frame did not have the expected frametype");
	ast_test_validate(test, frame_in->subclass.integer == AST_RTP_RTCP_PSFB,
		"REMB frame did not have the expected subclass integer");

	/* Verify the actual REMB information itself */
	received_feedback = frame_in->data.ptr;
	ast_test_validate(test, received_feedback->fmt == AST_RTP_RTCP_FMT_REMB,
		"REMB frame did not have the expected feedback format");
	ast_test_validate(test, received_feedback->remb.br_exp == feedback.remb.br_exp,
		"REMB received exponent did not match sent exponent");
	ast_test_validate(test, received_feedback->remb.br_mantissa == feedback.remb.br_mantissa,
		"REMB received mantissa did not match sent mantissa");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(sr_rr_nominal)
{
	RAII_VAR(struct ast_rtp_instance *, instance1, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_rtp_instance *, instance2, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_sched_context *, test_sched, NULL, ast_sched_context_destroy_wrapper);
	RAII_VAR(struct ast_frame *, frame_in, NULL, ast_frfree);

	switch (cmd) {
	case TEST_INIT:
		info->name = "sr_rr_nominal";
		info->category = "/res/res_rtp/";
		info->summary = "SR/RR nominal unit test";
		info->description =
			"Tests sending SR/RR and receiving it; includes SDES";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	test_sched = ast_sched_context_create();

	if ((test_init_rtp_instances(&instance1, &instance2, test_sched, TEST_TYPE_NONE)) < 0) {
		ast_log(LOG_ERROR, "Failed to initialize test!\n");
		return AST_TEST_FAIL;
	}

	test_write_and_read_frames(instance1, instance2, 1000, 10);

	/*
	 * Set the send_report flag so we send a sender report instead of normal RTP. We
	 * also need to ensure that SDES processed.
	 */
	ast_rtp_instance_queue_report(instance1);
	test_write_frames(instance1, 1010, 1);

	frame_in = ast_rtp_instance_read(instance2, 0);
	ast_test_validate(test, frame_in->frametype == AST_FRAME_RTCP,
		"Sender report frame did not have the expected frametype");
	ast_test_validate(test, frame_in->subclass.integer == AST_RTP_RTCP_SR,
		"Sender report frame did not have the expected subclass integer");
	ast_test_validate(test, ast_rtp_instance_get_sdes_received(instance2) == 1,
		"SDES was never processed for sender report");

	ast_frfree(frame_in);

	/* Set the send_report flag so we send a receiver report instead of normal RTP */
	ast_rtp_instance_queue_report(instance1);
	test_write_frames(instance1, 1010, 1);

	frame_in = ast_rtp_instance_read(instance2, 0);
	ast_test_validate(test, frame_in->frametype == AST_FRAME_RTCP,
		"Receiver report frame did not have the expected frametype");
	ast_test_validate(test, frame_in->subclass.integer == AST_RTP_RTCP_RR,
		"Receiver report frame did not have the expected subclass integer");

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(fir_nominal)
{
	RAII_VAR(struct ast_rtp_instance *, instance1, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_rtp_instance *, instance2, NULL, ast_rtp_instance_destroy);
	RAII_VAR(struct ast_sched_context *, test_sched, NULL, ast_sched_context_destroy_wrapper);
	RAII_VAR(struct ast_frame *, frame_in, NULL, ast_frfree);
	struct ast_frame frame_out = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = AST_CONTROL_VIDUPDATE,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "fir_nominal";
		info->category = "/res/res_rtp/";
		info->summary = "fir nominal unit test";
		info->description =
			"Tests sending and receiving a FIR packet";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	test_sched = ast_sched_context_create();

	if ((test_init_rtp_instances(&instance1, &instance2, test_sched, TEST_TYPE_NONE)) < 0) {
		ast_log(LOG_ERROR, "Failed to initialize test!\n");
		return AST_TEST_FAIL;
	}

	/* Send some packets to learn SSRC */
	test_write_and_read_frames(instance2, instance1, 1000, 10);

	/* The schedid must be 0 or greater, so let's do that now */
	ast_rtp_instance_set_schedid(instance1, 0);

	/*
	 * This will not directly write a frame out, but cause Asterisk to see it as a FIR
	 * request, which will then trigger rtp_write_rtcp_fir, which will send out the
	 * appropriate packet.
	 */
	ast_rtp_instance_write(instance1, &frame_out);

	/*
	 * We only receive one frame, the FIR request. It won't have a subclass integer of
	 * 206 (PSFB) because ast_rtcp_interpret sets it to 18 (AST_CONTROL_VIDUPDATE), so
	 * check for that.
	 */
	frame_in = ast_rtp_instance_read(instance2, 0);
	ast_test_validate(test, frame_in != NULL, "Did not receive a FIR frame");
	ast_test_validate(test, frame_in->frametype == AST_FRAME_CONTROL,
		"FIR frame did not have the expected frametype");
	ast_test_validate(test, frame_in->subclass.integer == AST_CONTROL_VIDUPDATE,
		"FIR frame did not have the expected subclass integer");

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(nack_no_packet_loss);
	AST_TEST_UNREGISTER(nack_nominal);
	AST_TEST_UNREGISTER(nack_overflow);
	AST_TEST_UNREGISTER(lost_packet_stats_nominal);
	AST_TEST_UNREGISTER(remb_nominal);
	AST_TEST_UNREGISTER(sr_rr_nominal);
	AST_TEST_UNREGISTER(fir_nominal);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(nack_no_packet_loss);
	AST_TEST_REGISTER(nack_nominal);
	AST_TEST_REGISTER(nack_overflow);
	AST_TEST_REGISTER(lost_packet_stats_nominal);
	AST_TEST_REGISTER(remb_nominal);
	AST_TEST_REGISTER(sr_rr_nominal);
	AST_TEST_REGISTER(fir_nominal);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "RTP/RTCP test module");
