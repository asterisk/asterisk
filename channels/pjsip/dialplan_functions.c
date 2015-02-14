/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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
 *
 * \author \verbatim Joshua Colp <jcolp@digium.com> \endverbatim
 * \author \verbatim Matt Jordan <mjordan@digium.com> \endverbatim
 *
 * \ingroup functions
 *
 * \brief PJSIP channel dialplan functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
<function name="PJSIP_DIAL_CONTACTS" language="en_US">
	<synopsis>
		Return a dial string for dialing all contacts on an AOR.
	</synopsis>
	<syntax>
		<parameter name="endpoint" required="true">
			<para>Name of the endpoint</para>
		</parameter>
		<parameter name="aor" required="false">
			<para>Name of an AOR to use, if not specified the configured AORs on the endpoint are used</para>
		</parameter>
		<parameter name="request_user" required="false">
			<para>Optional request user to use in the request URI</para>
		</parameter>
	</syntax>
	<description>
		<para>Returns a properly formatted dial string for dialing all contacts on an AOR.</para>
	</description>
</function>
<function name="PJSIP_MEDIA_OFFER" language="en_US">
	<synopsis>
		Media and codec offerings to be set on an outbound SIP channel prior to dialing.
	</synopsis>
	<syntax>
		<parameter name="media" required="true">
			<para>types of media offered</para>
		</parameter>
	</syntax>
	<description>
		<para>Returns the codecs offered based upon the media choice</para>
	</description>
</function>
<info name="PJSIPCHANNEL" language="en_US" tech="PJSIP">
	<enumlist>
		<enum name="rtp">
			<para>R/O Retrieve media related information.</para>
			<parameter name="type" required="true">
				<para>When <replaceable>rtp</replaceable> is specified, the
				<literal>type</literal> parameter must be provided. It specifies
				which RTP parameter to read.</para>
				<enumlist>
					<enum name="src">
						<para>Retrieve the local address for RTP.</para>
					</enum>
					<enum name="dest">
						<para>Retrieve the remote address for RTP.</para>
					</enum>
					<enum name="direct">
						<para>If direct media is enabled, this address is the remote address
						used for RTP.</para>
					</enum>
					<enum name="secure">
						<para>Whether or not the media stream is encrypted.</para>
						<enumlist>
							<enum name="0">
								<para>The media stream is not encrypted.</para>
							</enum>
							<enum name="1">
								<para>The media stream is encrypted.</para>
							</enum>
						</enumlist>
					</enum>
					<enum name="hold">
						<para>Whether or not the media stream is currently restricted
						due to a call hold.</para>
						<enumlist>
							<enum name="0">
								<para>The media stream is not held.</para>
							</enum>
							<enum name="1">
								<para>The media stream is held.</para>
							</enum>
						</enumlist>
					</enum>
				</enumlist>
			</parameter>
			<parameter name="media_type" required="false">
				<para>When <replaceable>rtp</replaceable> is specified, the
				<literal>media_type</literal> parameter may be provided. It specifies
				which media stream the chosen RTP parameter should be retrieved
				from.</para>
				<enumlist>
					<enum name="audio">
						<para>Retrieve information from the audio media stream.</para>
						<note><para>If not specified, <literal>audio</literal> is used
						by default.</para></note>
					</enum>
					<enum name="video">
						<para>Retrieve information from the video media stream.</para>
					</enum>
				</enumlist>
			</parameter>
		</enum>
		<enum name="rtcp">
			<para>R/O Retrieve RTCP statistics.</para>
			<parameter name="statistic" required="true">
				<para>When <replaceable>rtcp</replaceable> is specified, the
				<literal>statistic</literal> parameter must be provided. It specifies
				which RTCP statistic parameter to read.</para>
				<enumlist>
					<enum name="all">
						<para>Retrieve a summary of all RTCP statistics.</para>
						<para>The following data items are returned in a semi-colon
						delineated list:</para>
						<enumlist>
							<enum name="ssrc">
								<para>Our Synchronization Source identifier</para>
							</enum>
							<enum name="themssrc">
								<para>Their Synchronization Source identifier</para>
							</enum>
							<enum name="lp">
								<para>Our lost packet count</para>
							</enum>
							<enum name="rxjitter">
								<para>Received packet jitter</para>
							</enum>
							<enum name="rxcount">
								<para>Received packet count</para>
							</enum>
							<enum name="txjitter">
								<para>Transmitted packet jitter</para>
							</enum>
							<enum name="txcount">
								<para>Transmitted packet count</para>
							</enum>
							<enum name="rlp">
								<para>Remote lost packet count</para>
							</enum>
							<enum name="rtt">
								<para>Round trip time</para>
							</enum>
						</enumlist>
					</enum>
					<enum name="all_jitter">
						<para>Retrieve a summary of all RTCP Jitter statistics.</para>
						<para>The following data items are returned in a semi-colon
						delineated list:</para>
						<enumlist>
							<enum name="minrxjitter">
								<para>Our minimum jitter</para>
							</enum>
							<enum name="maxrxjitter">
								<para>Our max jitter</para>
							</enum>
							<enum name="avgrxjitter">
								<para>Our average jitter</para>
							</enum>
							<enum name="stdevrxjitter">
								<para>Our jitter standard deviation</para>
							</enum>
							<enum name="reported_minjitter">
								<para>Their minimum jitter</para>
							</enum>
							<enum name="reported_maxjitter">
								<para>Their max jitter</para>
							</enum>
							<enum name="reported_avgjitter">
								<para>Their average jitter</para>
							</enum>
							<enum name="reported_stdevjitter">
								<para>Their jitter standard deviation</para>
							</enum>
						</enumlist>
					</enum>
					<enum name="all_loss">
						<para>Retrieve a summary of all RTCP packet loss statistics.</para>
						<para>The following data items are returned in a semi-colon
						delineated list:</para>
						<enumlist>
							<enum name="minrxlost">
								<para>Our minimum lost packets</para>
							</enum>
							<enum name="maxrxlost">
								<para>Our max lost packets</para>
							</enum>
							<enum name="avgrxlost">
								<para>Our average lost packets</para>
							</enum>
							<enum name="stdevrxlost">
								<para>Our lost packets standard deviation</para>
							</enum>
							<enum name="reported_minlost">
								<para>Their minimum lost packets</para>
							</enum>
							<enum name="reported_maxlost">
								<para>Their max lost packets</para>
							</enum>
							<enum name="reported_avglost">
								<para>Their average lost packets</para>
							</enum>
							<enum name="reported_stdevlost">
								<para>Their lost packets standard deviation</para>
							</enum>
						</enumlist>
					</enum>
					<enum name="all_rtt">
						<para>Retrieve a summary of all RTCP round trip time information.</para>
						<para>The following data items are returned in a semi-colon
						delineated list:</para>
						<enumlist>
							<enum name="minrtt">
								<para>Minimum round trip time</para>
							</enum>
							<enum name="maxrtt">
								<para>Maximum round trip time</para>
							</enum>
							<enum name="avgrtt">
								<para>Average round trip time</para>
							</enum>
							<enum name="stdevrtt">
								<para>Standard deviation round trip time</para>
							</enum>
						</enumlist>
					</enum>
					<enum name="txcount"><para>Transmitted packet count</para></enum>
					<enum name="rxcount"><para>Received packet count</para></enum>
					<enum name="txjitter"><para>Transmitted packet jitter</para></enum>
					<enum name="rxjitter"><para>Received packet jitter</para></enum>
					<enum name="remote_maxjitter"><para>Their max jitter</para></enum>
					<enum name="remote_minjitter"><para>Their minimum jitter</para></enum>
					<enum name="remote_normdevjitter"><para>Their average jitter</para></enum>
					<enum name="remote_stdevjitter"><para>Their jitter standard deviation</para></enum>
					<enum name="local_maxjitter"><para>Our max jitter</para></enum>
					<enum name="local_minjitter"><para>Our minimum jitter</para></enum>
					<enum name="local_normdevjitter"><para>Our average jitter</para></enum>
					<enum name="local_stdevjitter"><para>Our jitter standard deviation</para></enum>
					<enum name="txploss"><para>Transmitted packet loss</para></enum>
					<enum name="rxploss"><para>Received packet loss</para></enum>
					<enum name="remote_maxrxploss"><para>Their max lost packets</para></enum>
					<enum name="remote_minrxploss"><para>Their minimum lost packets</para></enum>
					<enum name="remote_normdevrxploss"><para>Their average lost packets</para></enum>
					<enum name="remote_stdevrxploss"><para>Their lost packets standard deviation</para></enum>
					<enum name="local_maxrxploss"><para>Our max lost packets</para></enum>
					<enum name="local_minrxploss"><para>Our minimum lost packets</para></enum>
					<enum name="local_normdevrxploss"><para>Our average lost packets</para></enum>
					<enum name="local_stdevrxploss"><para>Our lost packets standard deviation</para></enum>
					<enum name="rtt"><para>Round trip time</para></enum>
					<enum name="maxrtt"><para>Maximum round trip time</para></enum>
					<enum name="minrtt"><para>Minimum round trip time</para></enum>
					<enum name="normdevrtt"><para>Average round trip time</para></enum>
					<enum name="stdevrtt"><para>Standard deviation round trip time</para></enum>
					<enum name="local_ssrc"><para>Our Synchronization Source identifier</para></enum>
					<enum name="remote_ssrc"><para>Their Synchronization Source identifier</para></enum>
				</enumlist>
			</parameter>
			<parameter name="media_type" required="false">
				<para>When <replaceable>rtcp</replaceable> is specified, the
				<literal>media_type</literal> parameter may be provided. It specifies
				which media stream the chosen RTCP parameter should be retrieved
				from.</para>
				<enumlist>
					<enum name="audio">
						<para>Retrieve information from the audio media stream.</para>
						<note><para>If not specified, <literal>audio</literal> is used
						by default.</para></note>
					</enum>
					<enum name="video">
						<para>Retrieve information from the video media stream.</para>
					</enum>
				</enumlist>
			</parameter>
		</enum>
		<enum name="endpoint">
			<para>R/O The name of the endpoint associated with this channel.
			Use the <replaceable>PJSIP_ENDPOINT</replaceable> function to obtain
			further endpoint related information.</para>
		</enum>
		<enum name="contact">
			<para>R/O The name of the contact associated with this channel.
			Use the <replaceable>PJSIP_CONTACT</replaceable> function to obtain
			further contact related information. Note this may not be present and if so
			is only available on outgoing legs.</para>
		</enum>
		<enum name="aor">
			<para>R/O The name of the AOR associated with this channel.
			Use the <replaceable>PJSIP_AOR</replaceable> function to obtain
			further AOR related information. Note this may not be present and if so
			is only available on outgoing legs.</para>
		</enum>
		<enum name="pjsip">
			<para>R/O Obtain information about the current PJSIP channel and its
			session.</para>
			<parameter name="type" required="true">
				<para>When <replaceable>pjsip</replaceable> is specified, the
				<literal>type</literal> parameter must be provided. It specifies
				which signalling parameter to read.</para>
				<enumlist>
					<enum name="secure">
						<para>Whether or not the signalling uses a secure transport.</para>
						<enumlist>
							<enum name="0"><para>The signalling uses a non-secure transport.</para></enum>
							<enum name="1"><para>The signalling uses a secure transport.</para></enum>
						</enumlist>
					</enum>
					<enum name="target_uri">
						<para>The request URI of the <literal>INVITE</literal> request associated with the creation of this channel.</para>
					</enum>
					<enum name="local_uri">
						<para>The local URI.</para>
					</enum>
					<enum name="remote_uri">
						<para>The remote URI.</para>
					</enum>
					<enum name="t38state">
						<para>The current state of any T.38 fax on this channel.</para>
						<enumlist>
							<enum name="DISABLED"><para>T.38 faxing is disabled on this channel.</para></enum>
							<enum name="LOCAL_REINVITE"><para>Asterisk has sent a <literal>re-INVITE</literal> to the remote end to initiate a T.38 fax.</para></enum>
							<enum name="REMOTE_REINVITE"><para>The remote end has sent a <literal>re-INVITE</literal> to Asterisk to initiate a T.38 fax.</para></enum>
							<enum name="ENABLED"><para>A T.38 fax session has been enabled.</para></enum>
							<enum name="REJECTED"><para>A T.38 fax session was attempted but was rejected.</para></enum>
						</enumlist>
					</enum>
					<enum name="local_addr">
						<para>On inbound calls, the full IP address and port number that
						the <literal>INVITE</literal> request was received on. On outbound
						calls, the full IP address and port number that the <literal>INVITE</literal>
						request was transmitted from.</para>
					</enum>
					<enum name="remote_addr">
						<para>On inbound calls, the full IP address and port number that
						the <literal>INVITE</literal> request was received from. On outbound
						calls, the full IP address and port number that the <literal>INVITE</literal>
						request was transmitted to.</para>
					</enum>
				</enumlist>
			</parameter>
		</enum>
	</enumlist>
</info>
***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjlib.h>
#include <pjsip_ua.h>

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/acl.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/format.h"
#include "asterisk/pbx.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "include/chan_pjsip.h"
#include "include/dialplan_functions.h"

/*!
 * \brief String representations of the T.38 state enum
 */
static const char *t38state_to_string[T38_MAX_ENUM] = {
	[T38_DISABLED] = "DISABLED",
	[T38_LOCAL_REINVITE] = "LOCAL_REINVITE",
	[T38_PEER_REINVITE] = "REMOTE_REINVITE",
	[T38_ENABLED] = "ENABLED",
	[T38_REJECTED] = "REJECTED",
};

/*!
 * \internal \brief Handle reading RTP information
 */
static int channel_read_rtp(struct ast_channel *chan, const char *type, const char *field, char *buf, size_t buflen)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(chan);
	struct chan_pjsip_pvt *pvt;
	struct ast_sip_session_media *media = NULL;
	struct ast_sockaddr addr;

	if (!channel) {
		ast_log(AST_LOG_WARNING, "Channel %s has no pvt!\n", ast_channel_name(chan));
		return -1;
	}

	pvt = channel->pvt;
	if (!pvt) {
		ast_log(AST_LOG_WARNING, "Channel %s has no chan_pjsip pvt!\n", ast_channel_name(chan));
		return -1;
	}

	if (ast_strlen_zero(type)) {
		ast_log(AST_LOG_WARNING, "You must supply a type field for 'rtp' information\n");
		return -1;
	}

	if (ast_strlen_zero(field) || !strcmp(field, "audio")) {
		media = pvt->media[SIP_MEDIA_AUDIO];
	} else if (!strcmp(field, "video")) {
		media = pvt->media[SIP_MEDIA_VIDEO];
	} else {
		ast_log(AST_LOG_WARNING, "Unknown media type field '%s' for 'rtp' information\n", field);
		return -1;
	}

	if (!media || !media->rtp) {
		ast_log(AST_LOG_WARNING, "Channel %s has no %s media/RTP session\n",
			ast_channel_name(chan), S_OR(field, "audio"));
		return -1;
	}

	if (!strcmp(type, "src")) {
		ast_rtp_instance_get_local_address(media->rtp, &addr);
		ast_copy_string(buf, ast_sockaddr_stringify(&addr), buflen);
	} else if (!strcmp(type, "dest")) {
		ast_rtp_instance_get_remote_address(media->rtp, &addr);
		ast_copy_string(buf, ast_sockaddr_stringify(&addr), buflen);
	} else if (!strcmp(type, "direct")) {
		ast_copy_string(buf, ast_sockaddr_stringify(&media->direct_media_addr), buflen);
	} else if (!strcmp(type, "secure")) {
		snprintf(buf, buflen, "%d", media->srtp ? 1 : 0);
	} else if (!strcmp(type, "hold")) {
		snprintf(buf, buflen, "%d", media->remotely_held ? 1 : 0);
	} else {
		ast_log(AST_LOG_WARNING, "Unknown type field '%s' specified for 'rtp' information\n", type);
		return -1;
	}

	return 0;
}

/*!
 * \internal \brief Handle reading RTCP information
 */
static int channel_read_rtcp(struct ast_channel *chan, const char *type, const char *field, char *buf, size_t buflen)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(chan);
	struct chan_pjsip_pvt *pvt;
	struct ast_sip_session_media *media = NULL;

	if (!channel) {
		ast_log(AST_LOG_WARNING, "Channel %s has no pvt!\n", ast_channel_name(chan));
		return -1;
	}

	pvt = channel->pvt;
	if (!pvt) {
		ast_log(AST_LOG_WARNING, "Channel %s has no chan_pjsip pvt!\n", ast_channel_name(chan));
		return -1;
	}

	if (ast_strlen_zero(type)) {
		ast_log(AST_LOG_WARNING, "You must supply a type field for 'rtcp' information\n");
		return -1;
	}

	if (ast_strlen_zero(field) || !strcmp(field, "audio")) {
		media = pvt->media[SIP_MEDIA_AUDIO];
	} else if (!strcmp(field, "video")) {
		media = pvt->media[SIP_MEDIA_VIDEO];
	} else {
		ast_log(AST_LOG_WARNING, "Unknown media type field '%s' for 'rtcp' information\n", field);
		return -1;
	}

	if (!media || !media->rtp) {
		ast_log(AST_LOG_WARNING, "Channel %s has no %s media/RTP session\n",
			ast_channel_name(chan), S_OR(field, "audio"));
		return -1;
	}

	if (!strncasecmp(type, "all", 3)) {
		enum ast_rtp_instance_stat_field stat_field = AST_RTP_INSTANCE_STAT_FIELD_QUALITY;

		if (!strcasecmp(type, "all_jitter")) {
			stat_field = AST_RTP_INSTANCE_STAT_FIELD_QUALITY_JITTER;
		} else if (!strcasecmp(type, "all_rtt")) {
			stat_field = AST_RTP_INSTANCE_STAT_FIELD_QUALITY_RTT;
		} else if (!strcasecmp(type, "all_loss")) {
			stat_field = AST_RTP_INSTANCE_STAT_FIELD_QUALITY_LOSS;
		}

		if (!ast_rtp_instance_get_quality(media->rtp, stat_field, buf, buflen)) {
			ast_log(AST_LOG_WARNING, "Unable to retrieve 'rtcp' statistics for %s\n", ast_channel_name(chan));
			return -1;
		}
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

		if (ast_rtp_instance_get_stats(media->rtp, &stats, AST_RTP_INSTANCE_STAT_ALL)) {
			ast_log(AST_LOG_WARNING, "Unable to retrieve 'rtcp' statistics for %s\n", ast_channel_name(chan));
			return -1;
		}

		for (i = 0; !ast_strlen_zero(lookup[i].name); i++) {
			if (!strcasecmp(type, lookup[i].name)) {
				if (lookup[i].type == INT) {
					snprintf(buf, buflen, "%u", *lookup[i].i4);
				} else {
					snprintf(buf, buflen, "%f", *lookup[i].d8);
				}
				return 0;
			}
		}
		ast_log(AST_LOG_WARNING, "Unrecognized argument '%s' for 'rtcp' information\n", type);
		return -1;
	}

	return 0;
}

/*!
 * \internal \brief Handle reading signalling information
 */
static int channel_read_pjsip(struct ast_channel *chan, const char *type, const char *field, char *buf, size_t buflen)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(chan);
	char *buf_copy;
	pjsip_dialog *dlg;

	if (!channel) {
		ast_log(AST_LOG_WARNING, "Channel %s has no pvt!\n", ast_channel_name(chan));
		return -1;
	}

	dlg = channel->session->inv_session->dlg;

	if (ast_strlen_zero(type)) {
		ast_log(LOG_WARNING, "You must supply a type field for 'pjsip' information\n");
		return -1;
	} else if (!strcmp(type, "secure")) {
#ifdef HAVE_PJSIP_GET_DEST_INFO
		pjsip_host_info dest;
		pj_pool_t *pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "secure-check", 128, 128);
		pjsip_get_dest_info(dlg->target, NULL, pool, &dest);
		snprintf(buf, buflen, "%d", dest.flag & PJSIP_TRANSPORT_SECURE ? 1 : 0);
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
#else
		ast_log(LOG_WARNING, "Asterisk has been built against a version of pjproject which does not have the required functionality to support the 'secure' argument. Please upgrade to version 2.3 or later.\n");
		return -1;
#endif
	} else if (!strcmp(type, "target_uri")) {
		pjsip_uri_print(PJSIP_URI_IN_REQ_URI, dlg->target, buf, buflen);
		buf_copy = ast_strdupa(buf);
		ast_escape_quoted(buf_copy, buf, buflen);
	} else if (!strcmp(type, "local_uri")) {
		pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, dlg->local.info->uri, buf, buflen);
		buf_copy = ast_strdupa(buf);
		ast_escape_quoted(buf_copy, buf, buflen);
	} else if (!strcmp(type, "remote_uri")) {
		pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, dlg->remote.info->uri, buf, buflen);
		buf_copy = ast_strdupa(buf);
		ast_escape_quoted(buf_copy, buf, buflen);
	} else if (!strcmp(type, "t38state")) {
		ast_copy_string(buf, t38state_to_string[channel->session->t38state], buflen);
	} else if (!strcmp(type, "local_addr")) {
		RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
		struct transport_info_data *transport_data;

		datastore = ast_sip_session_get_datastore(channel->session, "transport_info");
		if (!datastore) {
			ast_log(AST_LOG_WARNING, "No transport information for channel %s\n", ast_channel_name(chan));
			return -1;
		}
		transport_data = datastore->data;

		if (pj_sockaddr_has_addr(&transport_data->local_addr)) {
			pj_sockaddr_print(&transport_data->local_addr, buf, buflen, 3);
		}
	} else if (!strcmp(type, "remote_addr")) {
		RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
		struct transport_info_data *transport_data;

		datastore = ast_sip_session_get_datastore(channel->session, "transport_info");
		if (!datastore) {
			ast_log(AST_LOG_WARNING, "No transport information for channel %s\n", ast_channel_name(chan));
			return -1;
		}
		transport_data = datastore->data;

		if (pj_sockaddr_has_addr(&transport_data->remote_addr)) {
			pj_sockaddr_print(&transport_data->remote_addr, buf, buflen, 3);
		}
	} else {
		ast_log(AST_LOG_WARNING, "Unrecognized argument '%s' for 'pjsip' information\n", type);
		return -1;
	}

	return 0;
}

/*! \brief Struct used to push function arguments to task processor */
struct pjsip_func_args {
	struct ast_channel *chan;
	const char *param;
	const char *type;
	const char *field;
	char *buf;
	size_t len;
	int ret;
};

/*! \internal \brief Taskprocessor callback that handles the read on a PJSIP thread */
static int read_pjsip(void *data)
{
	struct pjsip_func_args *func_args = data;

	if (!strcmp(func_args->param, "rtp")) {
		func_args->ret = channel_read_rtp(func_args->chan, func_args->type,
		                                  func_args->field, func_args->buf,
		                                  func_args->len);
	} else if (!strcmp(func_args->param, "rtcp")) {
		func_args->ret = channel_read_rtcp(func_args->chan, func_args->type,
		                                   func_args->field, func_args->buf,
		                                   func_args->len);
	} else if (!strcmp(func_args->param, "endpoint")) {
		struct ast_sip_channel_pvt *pvt = ast_channel_tech_pvt(func_args->chan);

		if (!pvt) {
			ast_log(AST_LOG_WARNING, "Channel %s has no pvt!\n", ast_channel_name(func_args->chan));
			return -1;
		}
		if (!pvt->session || !pvt->session->endpoint) {
			ast_log(AST_LOG_WARNING, "Channel %s has no endpoint!\n", ast_channel_name(func_args->chan));
			return -1;
		}
		snprintf(func_args->buf, func_args->len, "%s", ast_sorcery_object_get_id(pvt->session->endpoint));
	} else if (!strcmp(func_args->param, "contact")) {
		struct ast_sip_channel_pvt *pvt = ast_channel_tech_pvt(func_args->chan);

		if (!pvt) {
			ast_log(AST_LOG_WARNING, "Channel %s has no pvt!\n", ast_channel_name(func_args->chan));
			return -1;
		}
		if (!pvt->session || !pvt->session->contact) {
			return 0;
		}
		snprintf(func_args->buf, func_args->len, "%s", ast_sorcery_object_get_id(pvt->session->contact));
	} else if (!strcmp(func_args->param, "aor")) {
		struct ast_sip_channel_pvt *pvt = ast_channel_tech_pvt(func_args->chan);

		if (!pvt) {
			ast_log(AST_LOG_WARNING, "Channel %s has no pvt!\n", ast_channel_name(func_args->chan));
			return -1;
		}
		if (!pvt->session || !pvt->session->aor) {
			return 0;
		}
		snprintf(func_args->buf, func_args->len, "%s", ast_sorcery_object_get_id(pvt->session->aor));
	} else if (!strcmp(func_args->param, "pjsip")) {
		func_args->ret = channel_read_pjsip(func_args->chan, func_args->type,
		                                    func_args->field, func_args->buf,
		                                    func_args->len);
	} else {
		func_args->ret = -1;
	}

	return 0;
}


int pjsip_acf_channel_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct pjsip_func_args func_args = { 0, };
	struct ast_sip_channel_pvt *channel;
	char *parse = ast_strdupa(data);

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(param);
		AST_APP_ARG(type);
		AST_APP_ARG(field);
	);

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}
	channel = ast_channel_tech_pvt(chan);

	/* Check for zero arguments */
	if (ast_strlen_zero(parse)) {
		ast_log(LOG_ERROR, "Cannot call %s without arguments\n", cmd);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);

	/* Sanity check */
	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		return 0;
	}

	if (!channel) {
		ast_log(AST_LOG_WARNING, "Channel %s has no pvt!\n", ast_channel_name(chan));
		return -1;
	}

	memset(buf, 0, len);

	func_args.chan = chan;
	func_args.param = args.param;
	func_args.type = args.type;
	func_args.field = args.field;
	func_args.buf = buf;
	func_args.len = len;
	if (ast_sip_push_task_synchronous(channel->session->serializer, read_pjsip, &func_args)) {
		ast_log(LOG_WARNING, "Unable to read properties of channel %s: failed to push task\n", ast_channel_name(chan));
		return -1;
	}

	return func_args.ret;
}

int pjsip_acf_dial_contacts_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	RAII_VAR(struct ast_str *, dial, NULL, ast_free_ptr);
	const char *aor_name;
	char *rest;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(endpoint_name);
		AST_APP_ARG(aor_name);
		AST_APP_ARG(request_user);
	);

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.endpoint_name)) {
		ast_log(LOG_WARNING, "An endpoint name must be specified when using the '%s' dialplan function\n", cmd);
		return -1;
	} else if (!(endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", args.endpoint_name))) {
		ast_log(LOG_WARNING, "Specified endpoint '%s' was not found\n", args.endpoint_name);
		return -1;
	}

	aor_name = S_OR(args.aor_name, endpoint->aors);

	if (ast_strlen_zero(aor_name)) {
		ast_log(LOG_WARNING, "No AOR has been provided and no AORs are configured on endpoint '%s'\n", args.endpoint_name);
		return -1;
	} else if (!(dial = ast_str_create(len))) {
		ast_log(LOG_WARNING, "Could not get enough buffer space for dialing contacts\n");
		return -1;
	} else if (!(rest = ast_strdupa(aor_name))) {
		ast_log(LOG_WARNING, "Could not duplicate provided AORs\n");
		return -1;
	}

	while ((aor_name = strsep(&rest, ","))) {
		RAII_VAR(struct ast_sip_aor *, aor, ast_sip_location_retrieve_aor(aor_name), ao2_cleanup);
		RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);
		struct ao2_iterator it_contacts;
		struct ast_sip_contact *contact;

		if (!aor) {
			/* If the AOR provided is not found skip it, there may be more */
			continue;
		} else if (!(contacts = ast_sip_location_retrieve_aor_contacts(aor))) {
			/* No contacts are available, skip it as well */
			continue;
		} else if (!ao2_container_count(contacts)) {
			/* We were given a container but no contacts are in it... */
			continue;
		}

		it_contacts = ao2_iterator_init(contacts, 0);
		for (; (contact = ao2_iterator_next(&it_contacts)); ao2_ref(contact, -1)) {
			ast_str_append(&dial, -1, "PJSIP/");

			if (!ast_strlen_zero(args.request_user)) {
				ast_str_append(&dial, -1, "%s@", args.request_user);
			}
			ast_str_append(&dial, -1, "%s/%s&", args.endpoint_name, contact->uri);
		}
		ao2_iterator_destroy(&it_contacts);
	}

	/* Trim the '&' at the end off */
	ast_str_truncate(dial, ast_str_strlen(dial) - 1);

	ast_copy_string(buf, ast_str_buffer(dial), len);

	return 0;
}

static int media_offer_read_av(struct ast_sip_session *session, char *buf,
			       size_t len, enum ast_media_type media_type)
{
	int i, size = 0;

	for (i = 0; i < ast_format_cap_count(session->req_caps); i++) {
		struct ast_format *fmt = ast_format_cap_get_format(session->req_caps, i);

		if (ast_format_get_type(fmt) != media_type) {
			ao2_ref(fmt, -1);
			continue;
		}

		/* add one since we'll include a comma */
		size = strlen(ast_format_get_name(fmt)) + 1;
		len -= size;
		if ((len) < 0) {
			ao2_ref(fmt, -1);
			break;
		}

		/* no reason to use strncat here since we have already ensured buf has
                   enough space, so strcat can be safely used */
		strcat(buf, ast_format_get_name(fmt));
		strcat(buf, ",");

		ao2_ref(fmt, -1);
	}

	if (size) {
		/* remove the extra comma */
		buf[strlen(buf) - 1] = '\0';
	}
	return 0;
}

struct media_offer_data {
	struct ast_sip_session *session;
	enum ast_media_type media_type;
	const char *value;
};

static int media_offer_write_av(void *obj)
{
	struct media_offer_data *data = obj;

	ast_format_cap_remove_by_type(data->session->req_caps, data->media_type);
	ast_format_cap_update_by_allow_disallow(data->session->req_caps, data->value, 1);

	return 0;
}

int pjsip_acf_media_offer_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_sip_channel_pvt *channel;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);

	if (!strcmp(data, "audio")) {
		return media_offer_read_av(channel->session, buf, len, AST_MEDIA_TYPE_AUDIO);
	} else if (!strcmp(data, "video")) {
		return media_offer_read_av(channel->session, buf, len, AST_MEDIA_TYPE_VIDEO);
	}

	return 0;
}

int pjsip_acf_media_offer_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_sip_channel_pvt *channel;
	struct media_offer_data mdata = {
		.value = value
	};

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);
	mdata.session = channel->session;

	if (!strcmp(data, "audio")) {
		mdata.media_type = AST_MEDIA_TYPE_AUDIO;
	} else if (!strcmp(data, "video")) {
		mdata.media_type = AST_MEDIA_TYPE_VIDEO;
	}

	return ast_sip_push_task_synchronous(channel->session->serializer, media_offer_write_av, &mdata);
}
