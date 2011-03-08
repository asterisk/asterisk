/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
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
 * \brief sip channel dialplan functions and unit tests
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <math.h>

#include "asterisk/channel.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/pbx.h"
#include "asterisk/acl.h"

#include "include/sip.h"
#include "include/globals.h"
#include "include/dialog.h"
#include "include/dialplan_functions.h"
#include "include/sip_utils.h"


int sip_acf_channel_read(struct ast_channel *chan, const char *funcname, char *preparse, char *buf, size_t buflen)
{
	struct sip_pvt *p = chan->tech_pvt;
	char *parse = ast_strdupa(preparse);
	int res = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(param);
		AST_APP_ARG(type);
		AST_APP_ARG(field);
	);
		
	/* Check for zero arguments */
	if (ast_strlen_zero(parse)) {
		ast_log(LOG_ERROR, "Cannot call %s without arguments\n", funcname);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);

	/* Sanity check */
	if (!IS_SIP_TECH(chan->tech)) {
		ast_log(LOG_ERROR, "Cannot call %s on a non-SIP channel\n", funcname);
		return 0;
	}

	memset(buf, 0, buflen);

	if (p == NULL) {
		return -1;
	}

	if (!strcasecmp(args.param, "peerip")) {
		ast_copy_string(buf, ast_sockaddr_isnull(&p->sa) ? "" : ast_sockaddr_stringify_addr(&p->sa), buflen);
	} else if (!strcasecmp(args.param, "recvip")) {
		ast_copy_string(buf, ast_sockaddr_isnull(&p->recv) ? "" : ast_sockaddr_stringify_addr(&p->recv), buflen);
	} else if (!strcasecmp(args.param, "from")) {
		ast_copy_string(buf, p->from, buflen);
	} else if (!strcasecmp(args.param, "uri")) {
		ast_copy_string(buf, p->uri, buflen);
	} else if (!strcasecmp(args.param, "useragent")) {
		ast_copy_string(buf, p->useragent, buflen);
	} else if (!strcasecmp(args.param, "peername")) {
		ast_copy_string(buf, p->peername, buflen);
	} else if (!strcasecmp(args.param, "t38passthrough")) {
		ast_copy_string(buf, (p->t38.state == T38_DISABLED) ? "0" : "1", buflen);
	} else if (!strcasecmp(args.param, "rtpdest")) {
		struct ast_sockaddr addr;
		struct ast_rtp_instance *stream;

		if (ast_strlen_zero(args.type))
			args.type = "audio";

		if (!strcasecmp(args.type, "audio"))
			stream = p->rtp;
		else if (!strcasecmp(args.type, "video"))
			stream = p->vrtp;
		else if (!strcasecmp(args.type, "text"))
			stream = p->trtp;
		else
			return -1;

		/* Return 0 to suppress a console warning message */
		if (!stream) {
			return 0;
		}

		ast_rtp_instance_get_remote_address(stream, &addr);
		snprintf(buf, buflen, "%s", ast_sockaddr_stringify(&addr));
	} else if (!strcasecmp(args.param, "rtpsource")) {
		struct ast_sockaddr sa;
		struct ast_rtp_instance *stream;

		if (ast_strlen_zero(args.type))
			args.type = "audio";

		if (!strcasecmp(args.type, "audio"))
			stream = p->rtp;
		else if (!strcasecmp(args.type, "video"))
			stream = p->vrtp;
		else if (!strcasecmp(args.type, "text"))
			stream = p->trtp;
		else
			return -1;

		/* Return 0 to suppress a console warning message */
		if (!stream) {
			return 0;
		}

		ast_rtp_instance_get_local_address(stream, &sa);

		if (ast_sockaddr_isnull(&sa)) {
			struct ast_sockaddr dest_sa;
			ast_rtp_instance_get_remote_address(stream, &dest_sa);
			ast_ouraddrfor(&dest_sa, &sa);
		}

		snprintf(buf, buflen, "%s", ast_sockaddr_stringify(&sa));
	} else if (!strcasecmp(args.param, "rtpqos")) {
		struct ast_rtp_instance *rtp = NULL;

		if (ast_strlen_zero(args.type)) {
			args.type = "audio";
		}

		if (!strcasecmp(args.type, "audio")) {
			rtp = p->rtp;
		} else if (!strcasecmp(args.type, "video")) {
			rtp = p->vrtp;
		} else if (!strcasecmp(args.type, "text")) {
			rtp = p->trtp;
		} else {
			return -1;
		}

		if (ast_strlen_zero(args.field) || !strcasecmp(args.field, "all")) {
			char quality_buf[AST_MAX_USER_FIELD], *quality;

			if (!(quality = ast_rtp_instance_get_quality(rtp, AST_RTP_INSTANCE_STAT_FIELD_QUALITY, quality_buf, sizeof(quality_buf)))) {
				return -1;
			}

			ast_copy_string(buf, quality_buf, buflen);
			return res;
		} else {
			struct ast_rtp_instance_stats stats;
			int i;
			struct {
				const char *name;
				enum { INT, DBL } type;
				union {
					unsigned int *i4;
					double *d8;
				};
			} lookup[] = {
				{ "txcount",               INT, { .i4 = &stats.txcount, }, },
				{ "rxcount",               INT, { .i4 = &stats.rxcount, }, },
				{ "txjitter",              DBL, { .d8 = &stats.txjitter, }, },
				{ "rxjitter",              DBL, { .d8 = &stats.rxjitter, }, },
				{ "remote_maxjitter",      DBL, { .d8 = &stats.remote_maxjitter, }, },
				{ "remote_minjitter",      DBL, { .d8 = &stats.remote_minjitter, }, },
				{ "remote_normdevjitter",  DBL, { .d8 = &stats.remote_normdevjitter, }, },
				{ "remote_stdevjitter",    DBL, { .d8 = &stats.remote_stdevjitter, }, },
				{ "local_maxjitter",       DBL, { .d8 = &stats.local_maxjitter, }, },
				{ "local_minjitter",       DBL, { .d8 = &stats.local_minjitter, }, },
				{ "local_normdevjitter",   DBL, { .d8 = &stats.local_normdevjitter, }, },
				{ "local_stdevjitter",     DBL, { .d8 = &stats.local_stdevjitter, }, },
				{ "txploss",               INT, { .i4 = &stats.txploss, }, },
				{ "rxploss",               INT, { .i4 = &stats.rxploss, }, },
				{ "remote_maxrxploss",     DBL, { .d8 = &stats.remote_maxrxploss, }, },
				{ "remote_minrxploss",     DBL, { .d8 = &stats.remote_minrxploss, }, },
				{ "remote_normdevrxploss", DBL, { .d8 = &stats.remote_normdevrxploss, }, },
				{ "remote_stdevrxploss",   DBL, { .d8 = &stats.remote_stdevrxploss, }, },
				{ "local_maxrxploss",      DBL, { .d8 = &stats.local_maxrxploss, }, },
				{ "local_minrxploss",      DBL, { .d8 = &stats.local_minrxploss, }, },
				{ "local_normdevrxploss",  DBL, { .d8 = &stats.local_normdevrxploss, }, },
				{ "local_stdevrxploss",    DBL, { .d8 = &stats.local_stdevrxploss, }, },
				{ "rtt",                   DBL, { .d8 = &stats.rtt, }, },
				{ "maxrtt",                DBL, { .d8 = &stats.maxrtt, }, },
				{ "minrtt",                DBL, { .d8 = &stats.minrtt, }, },
				{ "normdevrtt",            DBL, { .d8 = &stats.normdevrtt, }, },
				{ "stdevrtt",              DBL, { .d8 = &stats.stdevrtt, }, },
				{ "local_ssrc",            INT, { .i4 = &stats.local_ssrc, }, },
				{ "remote_ssrc",           INT, { .i4 = &stats.remote_ssrc, }, },
				{ NULL, },
			};

			if (ast_rtp_instance_get_stats(rtp, &stats, AST_RTP_INSTANCE_STAT_ALL)) {
				return -1;
			}

			for (i = 0; !ast_strlen_zero(lookup[i].name); i++) {
				if (!strcasecmp(args.field, lookup[i].name)) {
					if (lookup[i].type == INT) {
						snprintf(buf, buflen, "%u", *lookup[i].i4);
					} else {
						snprintf(buf, buflen, "%f", *lookup[i].d8);
					}
					return 0;
				}
			}
			ast_log(LOG_WARNING, "Unrecognized argument '%s' to %s\n", preparse, funcname);
			return -1;
		}
	} else if (!strcasecmp(args.param, "secure_signaling")) {
		snprintf(buf, buflen, "%s", p->socket.type == SIP_TRANSPORT_TLS ? "1" : "");
	} else if (!strcasecmp(args.param, "secure_media")) {
		snprintf(buf, buflen, "%s", p->srtp ? "1" : "");
	} else {
		res = -1;
	}
	return res;
}

#ifdef TEST_FRAMEWORK
static int test_sip_rtpqos_1_new(struct ast_rtp_instance *instance, struct ast_sched_context *sched, struct ast_sockaddr *addr, void *data)
{
	/* Needed to pass sanity checks */
	ast_rtp_instance_set_data(instance, data);
	return 0;
}

static int test_sip_rtpqos_1_destroy(struct ast_rtp_instance *instance)
{
	/* Needed to pass sanity checks */
	return 0;
}

static struct ast_frame *test_sip_rtpqos_1_read(struct ast_rtp_instance *instance, int rtcp)
{
	/* Needed to pass sanity checks */
	return &ast_null_frame;
}

static int test_sip_rtpqos_1_write(struct ast_rtp_instance *instance, struct ast_frame *frame)
{
	/* Needed to pass sanity checks */
	return 0;
}

static int test_sip_rtpqos_1_get_stat(struct ast_rtp_instance *instance, struct ast_rtp_instance_stats *stats, enum ast_rtp_instance_stat stat)
{
	struct ast_rtp_instance_stats *s = ast_rtp_instance_get_data(instance);
	memcpy(stats, s, sizeof(*stats));
	return 0;
}

AST_TEST_DEFINE(test_sip_rtpqos_1)
{
	int i, res = AST_TEST_PASS;
	struct ast_rtp_engine test_engine = {
		.name = "test",
		.new = test_sip_rtpqos_1_new,
		.destroy = test_sip_rtpqos_1_destroy,
		.read = test_sip_rtpqos_1_read,
		.write = test_sip_rtpqos_1_write,
		.get_stat = test_sip_rtpqos_1_get_stat,
	};
	struct ast_sockaddr sa = { {0, } };
	struct ast_rtp_instance_stats mine = { 0, };
	struct sip_pvt *p = NULL;
	struct ast_channel *chan = NULL;
	struct ast_str *varstr = NULL, *buffer = NULL;
	struct {
		const char *name;
		enum { INT, DBL } type;
		union {
			unsigned int *i4;
			double *d8;
		};
	} lookup[] = {
		{ "txcount",               INT, { .i4 = &mine.txcount, }, },
		{ "rxcount",               INT, { .i4 = &mine.rxcount, }, },
		{ "txjitter",              DBL, { .d8 = &mine.txjitter, }, },
		{ "rxjitter",              DBL, { .d8 = &mine.rxjitter, }, },
		{ "remote_maxjitter",      DBL, { .d8 = &mine.remote_maxjitter, }, },
		{ "remote_minjitter",      DBL, { .d8 = &mine.remote_minjitter, }, },
		{ "remote_normdevjitter",  DBL, { .d8 = &mine.remote_normdevjitter, }, },
		{ "remote_stdevjitter",    DBL, { .d8 = &mine.remote_stdevjitter, }, },
		{ "local_maxjitter",       DBL, { .d8 = &mine.local_maxjitter, }, },
		{ "local_minjitter",       DBL, { .d8 = &mine.local_minjitter, }, },
		{ "local_normdevjitter",   DBL, { .d8 = &mine.local_normdevjitter, }, },
		{ "local_stdevjitter",     DBL, { .d8 = &mine.local_stdevjitter, }, },
		{ "txploss",               INT, { .i4 = &mine.txploss, }, },
		{ "rxploss",               INT, { .i4 = &mine.rxploss, }, },
		{ "remote_maxrxploss",     DBL, { .d8 = &mine.remote_maxrxploss, }, },
		{ "remote_minrxploss",     DBL, { .d8 = &mine.remote_minrxploss, }, },
		{ "remote_normdevrxploss", DBL, { .d8 = &mine.remote_normdevrxploss, }, },
		{ "remote_stdevrxploss",   DBL, { .d8 = &mine.remote_stdevrxploss, }, },
		{ "local_maxrxploss",      DBL, { .d8 = &mine.local_maxrxploss, }, },
		{ "local_minrxploss",      DBL, { .d8 = &mine.local_minrxploss, }, },
		{ "local_normdevrxploss",  DBL, { .d8 = &mine.local_normdevrxploss, }, },
		{ "local_stdevrxploss",    DBL, { .d8 = &mine.local_stdevrxploss, }, },
		{ "rtt",                   DBL, { .d8 = &mine.rtt, }, },
		{ "maxrtt",                DBL, { .d8 = &mine.maxrtt, }, },
		{ "minrtt",                DBL, { .d8 = &mine.minrtt, }, },
		{ "normdevrtt",            DBL, { .d8 = &mine.normdevrtt, }, },
		{ "stdevrtt",              DBL, { .d8 = &mine.stdevrtt, }, },
		{ "local_ssrc",            INT, { .i4 = &mine.local_ssrc, }, },
		{ "remote_ssrc",           INT, { .i4 = &mine.remote_ssrc, }, },
		{ NULL, },
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "test_sip_rtpqos";
		info->category = "/channels/chan_sip/";
		info->summary = "Test retrieval of SIP RTP QOS stats";
		info->description =
			"Verify values in the RTP instance structure can be accessed through the dialplan.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_rtp_engine_register2(&test_engine, NULL);
	/* Have to associate this with a SIP pvt and an ast_channel */
	if (!(p = sip_alloc(NULL, NULL, 0, SIP_NOTIFY, NULL))) {
		res = AST_TEST_NOT_RUN;
		goto done;
	}

	if (!(p->rtp = ast_rtp_instance_new("test", sched, &bindaddr, &mine))) {
		res = AST_TEST_NOT_RUN;
		goto done;
	}
	ast_rtp_instance_set_remote_address(p->rtp, &sa);
	if (!(chan = ast_dummy_channel_alloc())) {
		res = AST_TEST_NOT_RUN;
		goto done;
	}
	chan->tech = &sip_tech;
	chan->tech_pvt = p;
	p->owner = chan;

	varstr = ast_str_create(16);
	buffer = ast_str_create(16);
	if (!varstr || !buffer) {
		res = AST_TEST_NOT_RUN;
		goto done;
	}

	/* Populate "mine" with values, then retrieve them with the CHANNEL dialplan function */
	for (i = 0; !ast_strlen_zero(lookup[i].name); i++) {
		ast_str_set(&varstr, 0, "${CHANNEL(rtpqos,audio,%s)}", lookup[i].name);
		if (lookup[i].type == INT) {
			int j;
			char cmpstr[256];
			for (j = 1; j < 25; j++) {
				*lookup[i].i4 = j;
				ast_str_substitute_variables(&buffer, 0, chan, ast_str_buffer(varstr));
				snprintf(cmpstr, sizeof(cmpstr), "%d", j);
				if (strcmp(cmpstr, ast_str_buffer(buffer))) {
					res = AST_TEST_FAIL;
					ast_test_status_update(test, "%s != %s != %s\n", ast_str_buffer(varstr), cmpstr, ast_str_buffer(buffer));
					break;
				}
			}
		} else {
			double j, cmpdbl = 0.0;
			for (j = 1.0; j < 10.0; j += 0.3) {
				*lookup[i].d8 = j;
				ast_str_substitute_variables(&buffer, 0, chan, ast_str_buffer(varstr));
				if (sscanf(ast_str_buffer(buffer), "%lf", &cmpdbl) != 1 || fabs(j - cmpdbl > .05)) {
					res = AST_TEST_FAIL;
					ast_test_status_update(test, "%s != %f != %s\n", ast_str_buffer(varstr), j, ast_str_buffer(buffer));
					break;
				}
			}
		}
	}

done:
	ast_free(varstr);
	ast_free(buffer);

	/* This unref will take care of destroying the channel, RTP instance, and SIP pvt */
	if (p) {
		dialog_unref(p, "Destroy test object");
	}
	ast_rtp_engine_unregister(&test_engine);
	return res;
}
#endif

/*! \brief SIP test registration */
void sip_dialplan_function_register_tests(void)
{
	AST_TEST_REGISTER(test_sip_rtpqos_1);
}

/*! \brief SIP test registration */
void sip_dialplan_function_unregister_tests(void)
{
	AST_TEST_UNREGISTER(test_sip_rtpqos_1);
}

