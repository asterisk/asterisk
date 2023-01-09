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
		<para>When read, returns the codecs offered based upon the media choice.</para>
		<para>When written, sets the codecs to offer when an outbound dial attempt is made,
		or when a session refresh is sent using <replaceable>PJSIP_SEND_SESSION_REFRESH</replaceable>.
		</para>
	</description>
	<see-also>
		<ref type="function">PJSIP_SEND_SESSION_REFRESH</ref>
	</see-also>
</function>
<function name="PJSIP_DTMF_MODE" language="en_US">
	<since>
		<version>13.18.0</version>
		<version>14.7.0</version>
		<version>15.1.0</version>
		<version>16.0.0</version>
	</since>
	<synopsis>
		Get or change the DTMF mode for a SIP call.
	</synopsis>
	<syntax>
	</syntax>
	<description>
		<para>When read, returns the current DTMF mode</para>
		<para>When written, sets the current DTMF mode</para>
		<para>This function uses the same DTMF mode naming as the dtmf_mode configuration option</para>
	</description>
</function>
<function name="PJSIP_MOH_PASSTHROUGH" language="en_US">
	<synopsis>
		Get or change the on-hold behavior for a SIP call.
	</synopsis>
	<syntax>
	</syntax>
	<description>
		<para>When read, returns the current moh passthrough mode</para>
		<para>When written, sets the current moh passthrough mode</para>
		<para>If <replaceable>yes</replaceable>, on-hold re-INVITEs are sent. If <replaceable>no</replaceable>, music on hold is generated.</para>
		<para>This function can be used to override the moh_passthrough configuration option</para>
	</description>
</function>
<function name="PJSIP_SEND_SESSION_REFRESH" language="en_US">
	<since>
		<version>13.12.0</version>
		<version>14.1.0</version>
		<version>15.0.0</version>
	</since>
	<synopsis>
		W/O: Initiate a session refresh via an UPDATE or re-INVITE on an established media session
	</synopsis>
	<syntax>
		<parameter name="update_type" required="false">
			<para>The type of update to send. Default is <literal>invite</literal>.</para>
			<enumlist>
				<enum name="invite">
					<para>Send the session refresh as a re-INVITE.</para>
				</enum>
				<enum name="update">
					<para>Send the session refresh as an UPDATE.</para>
				</enum>
			</enumlist>
		</parameter>
	</syntax>
	<description>
		<para>This function will cause the PJSIP stack to immediately refresh
		the media session for the channel. This will be done using either a
		re-INVITE (default) or an UPDATE request.
		</para>
		<para>This is most useful when combined with the <replaceable>PJSIP_MEDIA_OFFER</replaceable>
		dialplan function, as it allows the formats in use on a channel to be
		re-negotiated after call setup.</para>
		<warning>
			<para>The formats the endpoint supports are <emphasis>not</emphasis>
			checked or enforced by this function. Using this function to offer
			formats not supported by the endpoint <emphasis>may</emphasis> result
			in a loss of media.</para>
		</warning>
		<example title="Re-negotiate format to g722">
		 ; Within some existing extension on an answered channel
		 same => n,Set(PJSIP_MEDIA_OFFER(audio)=!all,g722)
		 same => n,Set(PJSIP_SEND_SESSION_REFRESH()=invite)
		</example>
	</description>
	<see-also>
		<ref type="function">PJSIP_MEDIA_OFFER</ref>
	</see-also>
</function>
<function name="PJSIP_PARSE_URI" language="en_US">
	<since>
		<version>13.24.0</version>
		<version>16.1.0</version>
		<version>17.0.0</version>
	</since>
	<synopsis>
		Parse an uri and return a type part of the URI.
	</synopsis>
	<syntax>
		<parameter name="uri" required="true">
			<para>URI to parse</para>
		</parameter>
		<parameter name="type" required="true">
			<para>The <literal>type</literal> parameter specifies which URI part to read</para>
			<enumlist>
				<enum name="display">
					<para>Display name.</para>
				</enum>
				<enum name="scheme">
					<para>URI scheme.</para>
				</enum>
				<enum name="user">
					<para>User part.</para>
				</enum>
				<enum name="passwd">
					<para>Password part.</para>
				</enum>
				<enum name="host">
					<para>Host part.</para>
				</enum>
				<enum name="port">
					<para>Port number, or zero.</para>
				</enum>
				<enum name="user_param">
					<para>User parameter.</para>
				</enum>
				<enum name="method_param">
					<para>Method parameter.</para>
				</enum>
				<enum name="transport_param">
					<para>Transport parameter.</para>
				</enum>
				<enum name="ttl_param">
					<para>TTL param, or -1.</para>
				</enum>
				<enum name="lr_param">
					<para>Loose routing param, or zero.</para>
				</enum>
				<enum name="maddr_param">
					<para>Maddr param.</para>
				</enum>
			</enumlist>
		</parameter>
	</syntax>
	<description>
		<para>Parse an URI and return a specified part of the URI.</para>
	</description>
</function>
<info name="CHANNEL" language="en_US" tech="PJSIP">
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
					<enum name="call-id">
						<para>The SIP call-id.</para>
					</enum>
					<enum name="secure">
						<para>Whether or not the signalling uses a secure transport.</para>
						<enumlist>
							<enum name="0"><para>The signalling uses a non-secure transport.</para></enum>
							<enum name="1"><para>The signalling uses a secure transport.</para></enum>
						</enumlist>
					</enum>
					<enum name="target_uri">
						<para>The contact URI where requests are sent.</para>
					</enum>
					<enum name="local_uri">
						<para>The local URI.</para>
					</enum>
					<enum name="local_tag">
						<para>Tag in From header</para>
					</enum>
					<enum name="remote_uri">
						<para>The remote URI.</para>
					</enum>
					<enum name="remote_tag">
						<para>Tag in To header</para>
					</enum>
					<enum name="request_uri">
						<para>The request URI of the incoming <literal>INVITE</literal>
						associated with the creation of this channel.</para>
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
<info name="CHANNEL_EXAMPLES" language="en_US" tech="PJSIP">
	<example title="PJSIP specific CHANNEL examples">
		; Log the current Call-ID
		same => n,Log(NOTICE, ${CHANNEL(pjsip,call-id)})

		; Log the destination address of the audio stream
		same => n,Log(NOTICE, ${CHANNEL(rtp,dest)})

		; Store the round-trip time associated with a
		; video stream in the CDR field video-rtt
		same => n,Set(CDR(video-rtt)=${CHANNEL(rtcp,rtt,video)})
	</example>
</info>
***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjlib.h>
#include <pjsip_ua.h>

#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/acl.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/stream.h"
#include "asterisk/format.h"
#include "asterisk/dsp.h"
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
	struct ast_sip_session *session;
	struct ast_sip_session_media *media;
	struct ast_sockaddr addr;

	if (!channel) {
		ast_log(AST_LOG_WARNING, "Channel %s has no pvt!\n", ast_channel_name(chan));
		return -1;
	}

	session = channel->session;
	if (!session) {
		ast_log(AST_LOG_WARNING, "Channel %s has no session!\n", ast_channel_name(chan));
		return -1;
	}

	if (ast_strlen_zero(type)) {
		ast_log(AST_LOG_WARNING, "You must supply a type field for 'rtp' information\n");
		return -1;
	}

	if (ast_strlen_zero(field) || !strcmp(field, "audio")) {
		media = session->active_media_state->default_session[AST_MEDIA_TYPE_AUDIO];
	} else if (!strcmp(field, "video")) {
		media = session->active_media_state->default_session[AST_MEDIA_TYPE_VIDEO];
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
		if (media->srtp) {
			struct ast_sdp_srtp *srtp = media->srtp;
			int flag = ast_test_flag(srtp, AST_SRTP_CRYPTO_OFFER_OK);
			snprintf(buf, buflen, "%d", flag ? 1 : 0);
		} else {
			snprintf(buf, buflen, "%d", 0);
		}
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
	struct ast_sip_session *session;
	struct ast_sip_session_media *media;

	if (!channel) {
		ast_log(AST_LOG_WARNING, "Channel %s has no pvt!\n", ast_channel_name(chan));
		return -1;
	}

	session = channel->session;
	if (!session) {
		ast_log(AST_LOG_WARNING, "Channel %s has no session!\n", ast_channel_name(chan));
		return -1;
	}

	if (ast_strlen_zero(type)) {
		ast_log(AST_LOG_WARNING, "You must supply a type field for 'rtcp' information\n");
		return -1;
	}

	if (ast_strlen_zero(field) || !strcmp(field, "audio")) {
		media = session->active_media_state->default_session[AST_MEDIA_TYPE_AUDIO];
	} else if (!strcmp(field, "video")) {
		media = session->active_media_state->default_session[AST_MEDIA_TYPE_VIDEO];
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

static int print_escaped_uri(struct ast_channel *chan, const char *type,
	pjsip_uri_context_e context, const void *uri, char *buf, size_t size)
{
	int res;
	char *buf_copy;

	res = pjsip_uri_print(context, uri, buf, size);
	if (res < 0) {
		ast_log(LOG_ERROR, "Channel %s: Unescaped %s too long for %d byte buffer\n",
			ast_channel_name(chan), type, (int) size);

		/* Empty buffer that likely is not terminated. */
		buf[0] = '\0';
		return -1;
	}

	buf_copy = ast_strdupa(buf);
	ast_escape_quoted(buf_copy, buf, size);
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
	int res = 0;

	if (!channel) {
		ast_log(AST_LOG_WARNING, "Channel %s has no pvt!\n", ast_channel_name(chan));
		return -1;
	}

	dlg = channel->session->inv_session->dlg;

	if (ast_strlen_zero(type)) {
		ast_log(LOG_WARNING, "You must supply a type field for 'pjsip' information\n");
		return -1;
	} else if (!strcmp(type, "call-id")) {
		snprintf(buf, buflen, "%.*s", (int) pj_strlen(&dlg->call_id->id), pj_strbuf(&dlg->call_id->id));
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
		res = print_escaped_uri(chan, type, PJSIP_URI_IN_REQ_URI, dlg->target, buf,
			buflen);
	} else if (!strcmp(type, "local_uri")) {
		res = print_escaped_uri(chan, type, PJSIP_URI_IN_FROMTO_HDR, dlg->local.info->uri,
			buf, buflen);
	} else if (!strcmp(type, "local_tag")) {
		ast_copy_pj_str(buf, &dlg->local.info->tag, buflen);
		buf_copy = ast_strdupa(buf);
		ast_escape_quoted(buf_copy, buf, buflen);
	} else if (!strcmp(type, "remote_uri")) {
		res = print_escaped_uri(chan, type, PJSIP_URI_IN_FROMTO_HDR,
			dlg->remote.info->uri, buf, buflen);
	} else if (!strcmp(type, "remote_tag")) {
		ast_copy_pj_str(buf, &dlg->remote.info->tag, buflen);
		buf_copy = ast_strdupa(buf);
		ast_escape_quoted(buf_copy, buf, buflen);
	} else if (!strcmp(type, "request_uri")) {
		if (channel->session->request_uri) {
			res = print_escaped_uri(chan, type, PJSIP_URI_IN_REQ_URI,
				channel->session->request_uri, buf, buflen);
		}
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

	return res;
}

/*! \brief Struct used to push function arguments to task processor */
struct pjsip_func_args {
	struct ast_sip_session *session;
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
		if (!func_args->session->channel) {
			func_args->ret = -1;
			return 0;
		}
		func_args->ret = channel_read_rtp(func_args->session->channel, func_args->type,
		                                  func_args->field, func_args->buf,
		                                  func_args->len);
	} else if (!strcmp(func_args->param, "rtcp")) {
		if (!func_args->session->channel) {
			func_args->ret = -1;
			return 0;
		}
		func_args->ret = channel_read_rtcp(func_args->session->channel, func_args->type,
		                                   func_args->field, func_args->buf,
		                                   func_args->len);
	} else if (!strcmp(func_args->param, "endpoint")) {
		if (!func_args->session->endpoint) {
			ast_log(AST_LOG_WARNING, "Channel %s has no endpoint!\n", func_args->session->channel ?
				ast_channel_name(func_args->session->channel) : "<unknown>");
			func_args->ret = -1;
			return 0;
		}
		snprintf(func_args->buf, func_args->len, "%s", ast_sorcery_object_get_id(func_args->session->endpoint));
	} else if (!strcmp(func_args->param, "contact")) {
		if (!func_args->session->contact) {
			return 0;
		}
		snprintf(func_args->buf, func_args->len, "%s", ast_sorcery_object_get_id(func_args->session->contact));
	} else if (!strcmp(func_args->param, "aor")) {
		if (!func_args->session->aor) {
			return 0;
		}
		snprintf(func_args->buf, func_args->len, "%s", ast_sorcery_object_get_id(func_args->session->aor));
	} else if (!strcmp(func_args->param, "pjsip")) {
		if (!func_args->session->channel) {
			func_args->ret = -1;
			return 0;
		}
		func_args->ret = channel_read_pjsip(func_args->session->channel, func_args->type,
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

	/* Check for zero arguments */
	if (ast_strlen_zero(parse)) {
		ast_log(LOG_ERROR, "Cannot call %s without arguments\n", cmd);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);

	ast_channel_lock(chan);

	/* Sanity check */
	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		ast_channel_unlock(chan);
		return 0;
	}

	channel = ast_channel_tech_pvt(chan);
	if (!channel) {
		ast_log(LOG_WARNING, "Channel %s has no pvt!\n", ast_channel_name(chan));
		ast_channel_unlock(chan);
		return -1;
	}

	if (!channel->session) {
		ast_log(LOG_WARNING, "Channel %s has no session\n", ast_channel_name(chan));
		ast_channel_unlock(chan);
		return -1;
	}

	func_args.session = ao2_bump(channel->session);
	ast_channel_unlock(chan);

	memset(buf, 0, len);

	func_args.param = args.param;
	func_args.type = args.type;
	func_args.field = args.field;
	func_args.buf = buf;
	func_args.len = len;
	if (ast_sip_push_task_wait_serializer(func_args.session->serializer, read_pjsip, &func_args)) {
		ast_log(LOG_WARNING, "Unable to read properties of channel %s: failed to push task\n", ast_channel_name(chan));
		ao2_ref(func_args.session, -1);
		return -1;
	}
	ao2_ref(func_args.session, -1);

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

	while ((aor_name = ast_strip(strsep(&rest, ",")))) {
		RAII_VAR(struct ast_sip_aor *, aor, ast_sip_location_retrieve_aor(aor_name), ao2_cleanup);
		RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);
		struct ao2_iterator it_contacts;
		struct ast_sip_contact *contact;

		if (!aor) {
			/* If the AOR provided is not found skip it, there may be more */
			continue;
		} else if (!(contacts = ast_sip_location_retrieve_aor_contacts_filtered(aor, AST_SIP_CONTACT_FILTER_REACHABLE))) {
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

/*! \brief Session refresh state information */
struct session_refresh_state {
	/*! \brief Created proposed media state */
	struct ast_sip_session_media_state *media_state;
};

/*! \brief Destructor for session refresh information */
static void session_refresh_state_destroy(void *obj)
{
	struct session_refresh_state *state = obj;

	ast_sip_session_media_state_free(state->media_state);
	ast_free(obj);
}

/*! \brief Datastore for attaching session refresh state information */
static const struct ast_datastore_info session_refresh_datastore = {
	.type = "pjsip_session_refresh",
	.destroy = session_refresh_state_destroy,
};

/*! \brief Helper function which retrieves or allocates a session refresh state information datastore */
static struct session_refresh_state *session_refresh_state_get_or_alloc(struct ast_sip_session *session)
{
	RAII_VAR(struct ast_datastore *, datastore, ast_sip_session_get_datastore(session, "pjsip_session_refresh"), ao2_cleanup);
	struct session_refresh_state *state;

	/* While the datastore refcount is decremented this is operating in the serializer so it will remain valid regardless */
	if (datastore) {
		return datastore->data;
	}

	if (!(datastore = ast_sip_session_alloc_datastore(&session_refresh_datastore, "pjsip_session_refresh"))
		|| !(datastore->data = ast_calloc(1, sizeof(struct session_refresh_state)))
		|| ast_sip_session_add_datastore(session, datastore)) {
		return NULL;
	}

	state = datastore->data;
	state->media_state = ast_sip_session_media_state_alloc();
	if (!state->media_state) {
		ast_sip_session_remove_datastore(session, "pjsip_session_refresh");
		return NULL;
	}
	state->media_state->topology = ast_stream_topology_clone(session->endpoint->media.topology);
	if (!state->media_state->topology) {
		ast_sip_session_remove_datastore(session, "pjsip_session_refresh");
		return NULL;
	}

	datastore->data = state;

	return state;
}

/*! \brief Struct used to push PJSIP_PARSE_URI function arguments to task processor */
struct parse_uri_args {
	const char *uri;
	const char *type;
	char *buf;
	size_t buflen;
	int ret;
};

/*! \internal \brief Taskprocessor callback that handles the PJSIP_PARSE_URI on a PJSIP thread */
static int parse_uri_cb(void *data)
{
	struct parse_uri_args *args = data;
	pj_pool_t *pool;
	pjsip_name_addr *uri;
	pjsip_sip_uri *sip_uri;
	pj_str_t tmp;

	args->ret = 0;

	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "ParseUri", 128, 128);
	if (!pool) {
		ast_log(LOG_ERROR, "Failed to allocate ParseUri endpoint pool.\n");
		args->ret = -1;
		return 0;
	}

	pj_strdup2_with_null(pool, &tmp, args->uri);
	uri = (pjsip_name_addr *)pjsip_parse_uri(pool, tmp.ptr, tmp.slen, PJSIP_PARSE_URI_AS_NAMEADDR);
	if (!uri || (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri))) {
		ast_log(LOG_WARNING, "Failed to parse URI '%s'\n", args->uri);
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		args->ret = -1;
		return 0;
	}

	if (!strcmp(args->type, "scheme")) {
		ast_copy_pj_str(args->buf, pjsip_uri_get_scheme(uri), args->buflen);
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return 0;
	} else if (!strcmp(args->type, "display")) {
		ast_copy_pj_str(args->buf, &uri->display, args->buflen);
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return 0;
	}

	sip_uri = pjsip_uri_get_uri(uri);
	if (!sip_uri) {
		ast_log(LOG_ERROR, "Failed to get an URI object for '%s'\n", args->uri);
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		args->ret = -1;
		return 0;
	}

	if (!strcmp(args->type, "user")) {
		ast_copy_pj_str(args->buf, &sip_uri->user, args->buflen);
	} else if (!strcmp(args->type, "passwd")) {
		ast_copy_pj_str(args->buf, &sip_uri->passwd, args->buflen);
	} else if (!strcmp(args->type, "host")) {
		ast_copy_pj_str(args->buf, &sip_uri->host, args->buflen);
	} else if (!strcmp(args->type, "port")) {
		snprintf(args->buf, args->buflen, "%d", sip_uri->port);
	} else if (!strcmp(args->type, "user_param")) {
		ast_copy_pj_str(args->buf, &sip_uri->user_param, args->buflen);
	} else if (!strcmp(args->type, "method_param")) {
		ast_copy_pj_str(args->buf, &sip_uri->method_param, args->buflen);
	} else if (!strcmp(args->type, "transport_param")) {
		ast_copy_pj_str(args->buf, &sip_uri->transport_param, args->buflen);
	} else if (!strcmp(args->type, "ttl_param")) {
		snprintf(args->buf, args->buflen, "%d", sip_uri->ttl_param);
	} else if (!strcmp(args->type, "lr_param")) {
		snprintf(args->buf, args->buflen, "%d", sip_uri->lr_param);
	} else if (!strcmp(args->type, "maddr_param")) {
		ast_copy_pj_str(args->buf, &sip_uri->maddr_param, args->buflen);
	} else {
		ast_log(AST_LOG_WARNING, "Unknown type part '%s' specified\n", args->type);
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		args->ret = -1;
		return 0;
	}

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);

	return 0;
}

int pjsip_acf_parse_uri_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t buflen)
{
	struct parse_uri_args func_args = { 0, };

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(uri_str);
		AST_APP_ARG(type);
	);

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.uri_str)) {
		ast_log(LOG_WARNING, "An URI must be specified when using the '%s' dialplan function\n", cmd);
		return -1;
	}

	if (ast_strlen_zero(args.type)) {
		ast_log(LOG_WARNING, "A type part of the URI must be specified when using the '%s' dialplan function\n", cmd);
		return -1;
	}

	memset(buf, 0, buflen);

	func_args.uri = args.uri_str;
	func_args.type = args.type;
	func_args.buf = buf;
	func_args.buflen = buflen;
	if (ast_sip_push_task_wait_serializer(NULL, parse_uri_cb, &func_args)) {
		ast_log(LOG_WARNING, "Unable to parse URI: failed to push task\n");
		return -1;
	}

	return func_args.ret;
}

static int media_offer_read_av(struct ast_sip_session *session, char *buf,
			       size_t len, enum ast_media_type media_type)
{
	struct ast_stream_topology *topology;
	int idx;
	struct ast_stream *stream = NULL;
	const struct ast_format_cap *caps;
	size_t accum = 0;

	if (session->inv_session->dlg->state == PJSIP_DIALOG_STATE_ESTABLISHED) {
		struct session_refresh_state *state;

		/* As we've already answered we need to store our media state until we are ready to send it */
		state = session_refresh_state_get_or_alloc(session);
		if (!state) {
			return -1;
		}
		topology = state->media_state->topology;
	} else {
		/* The session is not yet up so we are initially answering or offering */
		if (!session->pending_media_state->topology) {
			session->pending_media_state->topology = ast_stream_topology_clone(session->endpoint->media.topology);
			if (!session->pending_media_state->topology) {
				return -1;
			}
		}
		topology = session->pending_media_state->topology;
	}

	/* Find the first suitable stream */
	for (idx = 0; idx < ast_stream_topology_get_count(topology); ++idx) {
		stream = ast_stream_topology_get_stream(topology, idx);

		if (ast_stream_get_type(stream) != media_type ||
			ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED) {
			stream = NULL;
			continue;
		}

		break;
	}

	/* If no suitable stream then exit early */
	if (!stream) {
		buf[0] = '\0';
		return 0;
	}

	caps = ast_stream_get_formats(stream);

	/* Note: buf is not terminated while the string is being built. */
	for (idx = 0; idx < ast_format_cap_count(caps); ++idx) {
		struct ast_format *fmt;
		size_t size;

		fmt = ast_format_cap_get_format(caps, idx);

		/* Add one for a comma or terminator */
		size = strlen(ast_format_get_name(fmt)) + 1;
		if (len < size) {
			ao2_ref(fmt, -1);
			break;
		}

		/* Append the format name */
		strcpy(buf + accum, ast_format_get_name(fmt));/* Safe */
		ao2_ref(fmt, -1);

		accum += size;
		len -= size;

		/* The last comma on the built string will be set to the terminator. */
		buf[accum - 1] = ',';
	}

	/* Remove the trailing comma or terminate an empty buffer. */
	buf[accum ? accum - 1 : 0] = '\0';
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
	struct ast_stream_topology *topology;
	struct ast_stream *stream;
	struct ast_format_cap *caps;

	if (data->session->inv_session->dlg->state == PJSIP_DIALOG_STATE_ESTABLISHED) {
		struct session_refresh_state *state;

		/* As we've already answered we need to store our media state until we are ready to send it */
		state = session_refresh_state_get_or_alloc(data->session);
		if (!state) {
			return -1;
		}
		topology = state->media_state->topology;
	} else {
		/* The session is not yet up so we are initially answering or offering */
		if (!data->session->pending_media_state->topology) {
			data->session->pending_media_state->topology = ast_stream_topology_clone(data->session->endpoint->media.topology);
			if (!data->session->pending_media_state->topology) {
				return -1;
			}
		}
		topology = data->session->pending_media_state->topology;
	}

	/* XXX This method won't work when it comes time to do multistream support. The proper way to do this
	 * will either be to
	 * a) Alter all media streams of a particular type.
	 * b) Change the dialplan function to be able to specify which stream to alter and alter only that
	 * one stream
	 */
	stream = ast_stream_topology_get_first_stream_by_type(topology, data->media_type);
	if (!stream) {
		return 0;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		return -1;
	}

	ast_format_cap_append_from_cap(caps, ast_stream_get_formats(stream),
		AST_MEDIA_TYPE_UNKNOWN);
	ast_format_cap_remove_by_type(caps, data->media_type);
	ast_format_cap_update_by_allow_disallow(caps, data->value, 1);
	ast_stream_set_formats(stream, caps);
	ast_stream_set_metadata(stream, "pjsip_session_refresh", "force");
	ao2_ref(caps, -1);

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
	} else {
		/* Ensure that the buffer is empty */
		buf[0] = '\0';
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

	return ast_sip_push_task_wait_serializer(channel->session->serializer, media_offer_write_av, &mdata);
}

int pjsip_acf_dtmf_mode_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_sip_channel_pvt *channel;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		ast_channel_unlock(chan);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);

	if (ast_sip_dtmf_to_str(channel->session->dtmf, buf, len) < 0) {
		ast_log(LOG_WARNING, "Unknown DTMF mode %d on PJSIP channel %s\n", channel->session->dtmf, ast_channel_name(chan));
		ast_channel_unlock(chan);
		return -1;
	}

	ast_channel_unlock(chan);
	return 0;
}

int pjsip_acf_moh_passthrough_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_sip_channel_pvt *channel;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	if (len < 3) {
		ast_log(LOG_WARNING, "%s: buffer too small\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		ast_channel_unlock(chan);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);
	strncpy(buf, AST_YESNO(channel->session->moh_passthrough), len);

	ast_channel_unlock(chan);
	return 0;
}

struct refresh_data {
	struct ast_sip_session *session;
	enum ast_sip_session_refresh_method method;
};

static int sip_session_response_cb(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	struct ast_format *fmt;

	if (!session->channel) {
		/* Egads! */
		return 0;
	}

	fmt = ast_format_cap_get_best_by_type(ast_channel_nativeformats(session->channel), AST_MEDIA_TYPE_AUDIO);
	if (!fmt) {
		/* No format? That's weird. */
		return 0;
	}
	ast_channel_set_writeformat(session->channel, fmt);
	ast_channel_set_rawwriteformat(session->channel, fmt);
	ast_channel_set_readformat(session->channel, fmt);
	ast_channel_set_rawreadformat(session->channel, fmt);
	ao2_ref(fmt, -1);

	return 0;
}

static int dtmf_mode_refresh_cb(void *obj)
{
	struct refresh_data *data = obj;

	if (data->session->inv_session->state == PJSIP_INV_STATE_CONFIRMED) {
		ast_debug(3, "Changing DTMF mode on channel %s after OFFER/ANSWER completion. Sending session refresh\n", ast_channel_name(data->session->channel));

		ast_sip_session_refresh(data->session, NULL, NULL,
			sip_session_response_cb, data->method, 1, NULL);
	} else if (data->session->inv_session->state == PJSIP_INV_STATE_INCOMING) {
		ast_debug(3, "Changing DTMF mode on channel %s during OFFER/ANSWER exchange. Updating SDP answer\n", ast_channel_name(data->session->channel));
		ast_sip_session_regenerate_answer(data->session, NULL);
	}

	return 0;
}

int pjsip_acf_dtmf_mode_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_sip_channel_pvt *channel;
	struct ast_sip_session_media *media;
	int dsp_features = 0;
	int dtmf = -1;
	struct refresh_data rdata = {
			.method = AST_SIP_SESSION_REFRESH_METHOD_INVITE,
		};

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		ast_channel_unlock(chan);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);
	rdata.session = channel->session;

	dtmf = ast_sip_str_to_dtmf(value);

	if (dtmf == -1) {
		ast_log(LOG_WARNING, "Cannot set DTMF mode to '%s' on channel '%s' as value is invalid.\n", value,
			ast_channel_name(chan));
		ast_channel_unlock(chan);
		return -1;
	}

	if (channel->session->dtmf == dtmf) {
		/* DTMF mode unchanged, nothing to do! */
		ast_channel_unlock(chan);
		return 0;
	}

	channel->session->dtmf = dtmf;

	media = channel->session->active_media_state->default_session[AST_MEDIA_TYPE_AUDIO];

	if (media && media->rtp) {
		if (channel->session->dtmf == AST_SIP_DTMF_RFC_4733) {
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_DTMF, 1);
			ast_rtp_instance_dtmf_mode_set(media->rtp, AST_RTP_DTMF_MODE_RFC2833);
		} else if (channel->session->dtmf == AST_SIP_DTMF_INFO) {
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_DTMF, 0);
			ast_rtp_instance_dtmf_mode_set(media->rtp, AST_RTP_DTMF_MODE_NONE);
		} else if (channel->session->dtmf == AST_SIP_DTMF_INBAND) {
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_DTMF, 0);
			ast_rtp_instance_dtmf_mode_set(media->rtp, AST_RTP_DTMF_MODE_INBAND);
		} else if (channel->session->dtmf == AST_SIP_DTMF_NONE) {
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_DTMF, 0);
			ast_rtp_instance_dtmf_mode_set(media->rtp, AST_RTP_DTMF_MODE_NONE);
		} else if (channel->session->dtmf == AST_SIP_DTMF_AUTO) {
			if (ast_rtp_instance_dtmf_mode_get(media->rtp) != AST_RTP_DTMF_MODE_RFC2833) {
				/* no RFC4733 negotiated, enable inband */
				ast_rtp_instance_dtmf_mode_set(media->rtp, AST_RTP_DTMF_MODE_INBAND);
			}
		} else if (channel->session->dtmf == AST_SIP_DTMF_AUTO_INFO) {
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_DTMF, 0);
			if (ast_rtp_instance_dtmf_mode_get(media->rtp) == AST_RTP_DTMF_MODE_INBAND) {
				/* if inband, switch to INFO */
				ast_rtp_instance_dtmf_mode_set(media->rtp, AST_RTP_DTMF_MODE_NONE);
			}
		}
	}

	if (channel->session->dsp) {
		dsp_features = ast_dsp_get_features(channel->session->dsp);
	}
	if (channel->session->dtmf == AST_SIP_DTMF_INBAND ||
		channel->session->dtmf == AST_SIP_DTMF_AUTO) {
		dsp_features |= DSP_FEATURE_DIGIT_DETECT;
	} else {
		dsp_features &= ~DSP_FEATURE_DIGIT_DETECT;
	}
	if (dsp_features) {
		if (!channel->session->dsp) {
			if (!(channel->session->dsp = ast_dsp_new())) {
				ast_channel_unlock(chan);
				return 0;
			}
		}
		ast_dsp_set_features(channel->session->dsp, dsp_features);
	} else if (channel->session->dsp) {
		ast_dsp_free(channel->session->dsp);
		channel->session->dsp = NULL;
	}

	ast_channel_unlock(chan);

	return ast_sip_push_task_wait_serializer(channel->session->serializer, dtmf_mode_refresh_cb, &rdata);
}

int pjsip_acf_moh_passthrough_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_sip_channel_pvt *channel;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		ast_channel_unlock(chan);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);
	channel->session->moh_passthrough = ast_true(value);

	ast_channel_unlock(chan);

	return 0;
}

static int refresh_write_cb(void *obj)
{
	struct refresh_data *data = obj;
	struct session_refresh_state *state;

	state = session_refresh_state_get_or_alloc(data->session);
	if (!state) {
		return -1;
	}

	ast_sip_session_refresh(data->session, NULL, NULL,
		sip_session_response_cb, data->method, 1, state->media_state);

	state->media_state = NULL;
	ast_sip_session_remove_datastore(data->session, "pjsip_session_refresh");

	return 0;
}

int pjsip_acf_session_refresh_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_sip_channel_pvt *channel;
	struct refresh_data rdata = {
		.method = AST_SIP_SESSION_REFRESH_METHOD_INVITE,
	};

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_log(LOG_WARNING, "'%s' not allowed on unanswered channel '%s'.\n", cmd, ast_channel_name(chan));
		return -1;
	}

	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);
	rdata.session = channel->session;

	if (!strcmp(value, "invite")) {
		rdata.method = AST_SIP_SESSION_REFRESH_METHOD_INVITE;
	} else if (!strcmp(value, "update")) {
		rdata.method = AST_SIP_SESSION_REFRESH_METHOD_UPDATE;
	}

	return ast_sip_push_task_wait_serializer(channel->session->serializer, refresh_write_cb, &rdata);
}
