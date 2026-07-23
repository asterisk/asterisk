/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Remi Quezada
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
 * \brief PJSIP direct media payload tests
 *
 * \author Remi Quezada
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>pjproject</depend>
	<depend>res_format_attr_opus</depend>
	<depend>res_pjsip_session</depend>
	<depend>res_rtp_asterisk</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/format.h"
#include "asterisk/format_cache.h"
#include "asterisk/module.h"
#include "asterisk/netsock2.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/sched.h"
#include "asterisk/strings.h"
#include "asterisk/test.h"

AST_TEST_DEFINE(payload_mapping_exchange)
{
	struct ast_sched_context *sched = NULL;
	struct ast_rtp_instance *rtp1 = NULL;
	struct ast_rtp_instance *rtp2 = NULL;
	struct ast_rtp_payload_type *type1 = NULL;
	struct ast_rtp_payload_type *type2 = NULL;
	struct ast_rtp_payload_type *unexpected = NULL;
	struct ast_sip_session_media media1 = { 0, };
	struct ast_sip_session_media media2 = { 0, };
	struct ast_format *opus1 = NULL;
	struct ast_format *opus2 = NULL;
	struct ast_sockaddr address;
	struct ast_str *fmtp = NULL;
	enum ast_test_result_state result = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "payload_mapping_exchange";
		info->category = "/res/res_pjsip_session/";
		info->summary = "Direct media common payload mapping";
		info->description = "Tests that both direct media legs use the same payload mappings";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

#define TEST_CHECK(condition, message) \
	do { \
		if (!(condition)) { \
			ast_test_status_update(test, "%s\n", message); \
			result = AST_TEST_FAIL; \
			goto cleanup; \
		} \
	} while (0)

	sched = ast_sched_context_create();
	TEST_CHECK(sched != NULL, "Unable to create scheduler context");
	ast_sockaddr_parse(&address, "127.0.0.1", 0);
	rtp1 = ast_rtp_instance_new("asterisk", sched, &address, "payload-test-1");
	rtp2 = ast_rtp_instance_new("asterisk", sched, &address, "payload-test-2");
	TEST_CHECK(rtp1 && rtp2, "Unable to create RTP instances");
	ast_rtp_instance_set_channel_id(rtp1, "A");
	ast_rtp_instance_set_channel_id(rtp2, "B");
	ast_rtp_codecs_payloads_clear(ast_rtp_instance_get_codecs(rtp1), rtp1);
	ast_rtp_codecs_payloads_clear(ast_rtp_instance_get_codecs(rtp2), rtp2);

	opus1 = ast_format_parse_sdp_fmtp(ast_format_opus,
		"maxplaybackrate=16000;maxaveragebitrate=64000;stereo=0;useinbandfec=0");
	opus2 = ast_format_parse_sdp_fmtp(ast_format_opus,
		"maxplaybackrate=48000;maxaveragebitrate=32000;stereo=1;useinbandfec=1");
	TEST_CHECK(opus1 && opus2, "Unable to create Opus formats");
	TEST_CHECK(!ast_rtp_codecs_payloads_set_rtpmap_type_rate(
		ast_rtp_instance_get_codecs(rtp1), rtp1, 99,
		"audio", "opus", 0, 48000), "Unable to set first Opus mapping");
	TEST_CHECK(!ast_rtp_codecs_payload_replace_format(
		ast_rtp_instance_get_codecs(rtp1), 99, opus1),
		"Unable to set first Opus attributes");
	TEST_CHECK(!ast_rtp_codecs_payloads_set_rtpmap_type_rate(
		ast_rtp_instance_get_codecs(rtp2), rtp2, 107,
		"audio", "opus", 0, 48000), "Unable to set second Opus mapping");
	TEST_CHECK(!ast_rtp_codecs_payload_replace_format(
		ast_rtp_instance_get_codecs(rtp2), 107, opus2),
		"Unable to set second Opus attributes");

	TEST_CHECK(!ast_rtp_codecs_payloads_set_rtpmap_type_rate(
		ast_rtp_instance_get_codecs(rtp1), rtp1, 101,
		"audio", "telephone-event", 0, 8000), "Unable to set first 8 kHz DTMF mapping");
	TEST_CHECK(!ast_rtp_codecs_payload_set_fmtp(
		ast_rtp_instance_get_codecs(rtp1), 101, "0-15"),
		"Unable to set first 8 kHz DTMF events");
	TEST_CHECK(!ast_rtp_codecs_payloads_set_rtpmap_type_rate(
		ast_rtp_instance_get_codecs(rtp1), rtp1, 108,
		"audio", "telephone-event", 0, 48000), "Unable to set first 48 kHz DTMF mapping");
	TEST_CHECK(!ast_rtp_codecs_payload_set_fmtp(
		ast_rtp_instance_get_codecs(rtp1), 108, "0-15"),
		"Unable to set first 48 kHz DTMF events");
	TEST_CHECK(!ast_rtp_codecs_payloads_set_rtpmap_type_rate(
		ast_rtp_instance_get_codecs(rtp2), rtp2, 103,
		"audio", "telephone-event", 0, 8000), "Unable to set second 8 kHz DTMF mapping");
	TEST_CHECK(!ast_rtp_codecs_payload_set_fmtp(
		ast_rtp_instance_get_codecs(rtp2), 103, "0-16"),
		"Unable to set second 8 kHz DTMF events");
	TEST_CHECK(!ast_rtp_codecs_payloads_set_rtpmap_type_rate(
		ast_rtp_instance_get_codecs(rtp2), rtp2, 101,
		"audio", "telephone-event", 0, 48000), "Unable to set second 48 kHz DTMF mapping");
	TEST_CHECK(!ast_rtp_codecs_payload_set_fmtp(
		ast_rtp_instance_get_codecs(rtp2), 101, "0-16"),
		"Unable to set second 48 kHz DTMF events");

	media1.rtp = rtp1;
	media2.rtp = rtp2;
	TEST_CHECK(ast_sip_session_media_set_direct_media_payloads(&media1, rtp2) == 1,
		"Unable to create first common payload snapshot");
	TEST_CHECK(ast_sip_session_media_set_direct_media_payloads(&media2, rtp1) == 1,
		"Unable to create second common payload snapshot");

	type1 = ast_sip_session_media_get_direct_media_payload(&media1, 99);
	type2 = ast_sip_session_media_get_direct_media_payload(&media2, 99);
	TEST_CHECK(type1 && type2 && type1->asterisk_format && type2->asterisk_format
		&& ast_format_cmp(type1->format, type2->format) != AST_FORMAT_CMP_NOT_EQUAL,
		"The two legs did not select the same Opus payload and attributes");
	fmtp = ast_str_create(128);
	TEST_CHECK(fmtp != NULL, "Unable to allocate Opus fmtp output");
	ast_format_generate_sdp_fmtp(type1->format, 99, &fmtp);
	TEST_CHECK(strstr(ast_str_buffer(fmtp), "maxplaybackrate=16000")
		&& strstr(ast_str_buffer(fmtp), "maxaveragebitrate=32000"),
		"The common Opus attributes were not negotiated");
	unexpected = ast_sip_session_media_get_direct_media_payload(&media1, 107);
	TEST_CHECK(!unexpected, "The first leg retained the non-canonical Opus payload");
	unexpected = ast_sip_session_media_get_direct_media_payload(&media2, 107);
	TEST_CHECK(!unexpected, "The second leg retained the non-canonical Opus payload");

	ao2_cleanup(type1);
	type1 = ast_sip_session_media_get_direct_media_payload(&media1, 101);
	ao2_cleanup(type2);
	type2 = ast_sip_session_media_get_direct_media_payload(&media2, 101);
	TEST_CHECK(type1 && type2 && type1->sample_rate == 8000 && type2->sample_rate == 8000
		&& type1->fmtp && type2->fmtp
		&& !strcmp(type1->fmtp, "0-15") && !strcmp(type2->fmtp, "0-15"),
		"The two legs did not select the same 8 kHz DTMF mapping");
	ao2_cleanup(type1);
	type1 = ast_sip_session_media_get_direct_media_payload(&media1, 108);
	ao2_cleanup(type2);
	type2 = ast_sip_session_media_get_direct_media_payload(&media2, 108);
	TEST_CHECK(type1 && type2 && type1->sample_rate == 48000 && type2->sample_rate == 48000
		&& type1->fmtp && type2->fmtp
		&& !strcmp(type1->fmtp, "0-15") && !strcmp(type2->fmtp, "0-15"),
		"The two legs did not select the same 48 kHz DTMF mapping");

cleanup:
	ast_sip_session_media_set_direct_media_payloads(&media1, NULL);
	ast_sip_session_media_set_direct_media_payloads(&media2, NULL);
	ao2_cleanup(unexpected);
	ao2_cleanup(type2);
	ao2_cleanup(type1);
	ast_free(fmtp);
	ao2_cleanup(opus2);
	ao2_cleanup(opus1);
	if (rtp2) {
		ast_rtp_instance_destroy(rtp2);
	}
	if (rtp1) {
		ast_rtp_instance_destroy(rtp1);
	}
	if (sched) {
		ast_sched_context_destroy(sched);
	}
#undef TEST_CHECK
	return result;
}

static int load_module(void)
{
	AST_TEST_REGISTER(payload_mapping_exchange);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(payload_mapping_exchange);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "PJSIP direct media payload tests",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_format_attr_opus,res_pjsip_session,res_rtp_asterisk",
);
