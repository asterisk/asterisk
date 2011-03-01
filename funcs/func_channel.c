/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
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
 * \brief Channel info dialplan functions
 *
 * \author Kevin P. Fleming <kpfleming@digium.com>
 * \author Ben Winslow
 * 
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <regex.h>
#include <ctype.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/indications.h"
#include "asterisk/stringfields.h"
#include "asterisk/global_datastores.h"

/*** DOCUMENTATION
	<function name="CHANNELS" language="en_US">
		<synopsis>
			Gets the list of channels, optionally filtering by a regular expression.
		</synopsis>
		<syntax>
			<parameter name="regular_expression" />
		</syntax>
		<description>
			<para>Gets the list of channels, optionally filtering by a <replaceable>regular_expression</replaceable>. If
			no argument is provided, all known channels are returned. The
			<replaceable>regular_expression</replaceable> must correspond to
			the POSIX.2 specification, as shown in <emphasis>regex(7)</emphasis>. The list returned
			will be space-delimited.</para>
		</description>
	</function>
	<function name="MASTER_CHANNEL" language="en_US">
		<synopsis>
			Gets or sets variables on the master channel
		</synopsis>
		<description>
			<para>Allows access to the channel which created the current channel, if any.  If the channel is already
			a master channel, then accesses local channel variables.</para>
		</description>
	</function>
	<function name="CHANNEL" language="en_US">
		<synopsis>
			Gets/sets various pieces of information about the channel.
		</synopsis>
		<syntax>
			<parameter name="item" required="true">
				<para>Standard items (provided by all channel technologies) are:</para>
				<enumlist>
					<enum name="audioreadformat">
						<para>R/O format currently being read.</para>
					</enum>
					<enum name="audionativeformat">
						<para>R/O format used natively for audio.</para>
					</enum>
					<enum name="audiowriteformat">
						<para>R/O format currently being written.</para>
					</enum>
					<enum name="callgroup">
						<para>R/W call groups for call pickup.</para>
					</enum>
					<enum name="channeltype">
						<para>R/O technology used for channel.</para>
					</enum>
					<enum name="checkhangup">
						<para>R/O Whether the channel is hanging up (1/0)</para>
					</enum>
					<enum name="language">
						<para>R/W language for sounds played.</para>
					</enum>
					<enum name="musicclass">
						<para>R/W class (from musiconhold.conf) for hold music.</para>
					</enum>
					<enum name="name">
						<para>The name of the channel</para>
					</enum>
					<enum name="parkinglot">
						<para>R/W parkinglot for parking.</para>
					</enum>
					<enum name="rxgain">
						<para>R/W set rxgain level on channel drivers that support it.</para>
					</enum>
					<enum name="secure_bridge_signaling">
						<para>Whether or not channels bridged to this channel require secure signaling</para>
					</enum>
					<enum name="secure_bridge_media">
						<para>Whether or not channels bridged to this channel require secure media</para>
					</enum>
					<enum name="state">
						<para>R/O state for channel</para>
					</enum>
					<enum name="tonezone">
						<para>R/W zone for indications played</para>
					</enum>
					<enum name="transfercapability">
						<para>R/W ISDN Transfer Capability, one of:</para>
						<enumlist>
							<enum name="SPEECH" />
							<enum name="DIGITAL" />
							<enum name="RESTRICTED_DIGITAL" />
							<enum name="3K1AUDIO" />
							<enum name="DIGITAL_W_TONES" />
							<enum name="VIDEO" />
						</enumlist>
					</enum>
					<enum name="txgain">
						<para>R/W set txgain level on channel drivers that support it.</para>
					</enum>
					<enum name="videonativeformat">
						<para>R/O format used natively for video</para>
					</enum>
					<enum name="trace">
						<para>R/W whether or not context tracing is enabled, only available
						<emphasis>if CHANNEL_TRACE is defined</emphasis>.</para>
					</enum>
				</enumlist>
				<para><emphasis>chan_sip</emphasis> provides the following additional options:</para>
				<enumlist>
					<enum name="peerip">
						<para>R/O Get the IP address of the peer.</para>
					</enum>
					<enum name="recvip">
						<para>R/O Get the source IP address of the peer.</para>
					</enum>
					<enum name="from">
						<para>R/O Get the URI from the From: header.</para>
					</enum>
					<enum name="uri">
						<para>R/O Get the URI from the Contact: header.</para>
					</enum>
					<enum name="useragent">
						<para>R/O Get the useragent.</para>
					</enum>
					<enum name="peername">
						<para>R/O Get the name of the peer.</para>
					</enum>
					<enum name="t38passthrough">
						<para>R/O <literal>1</literal> if T38 is offered or enabled in this channel,
						otherwise <literal>0</literal></para>
					</enum>
					<enum name="rtpqos">
						<para>R/O Get QOS information about the RTP stream</para>
						<para>    This option takes two additional arguments:</para>
						<para>    Argument 1:</para>
						<para>     <literal>audio</literal>             Get data about the audio stream</para>
						<para>     <literal>video</literal>             Get data about the video stream</para>
						<para>     <literal>text</literal>              Get data about the text stream</para>
						<para>    Argument 2:</para>
						<para>     <literal>local_ssrc</literal>        Local SSRC (stream ID)</para>
						<para>     <literal>local_lostpackets</literal> Local lost packets</para>
						<para>     <literal>local_jitter</literal>      Local calculated jitter</para>
						<para>     <literal>local_maxjitter</literal>   Local calculated jitter (maximum)</para>
						<para>     <literal>local_minjitter</literal>   Local calculated jitter (minimum)</para>
						<para>     <literal>local_normdevjitter</literal>Local calculated jitter (normal deviation)</para>
						<para>     <literal>local_stdevjitter</literal> Local calculated jitter (standard deviation)</para>
						<para>     <literal>local_count</literal>       Number of received packets</para>
						<para>     <literal>remote_ssrc</literal>       Remote SSRC (stream ID)</para>
						<para>     <literal>remote_lostpackets</literal>Remote lost packets</para>
						<para>     <literal>remote_jitter</literal>     Remote reported jitter</para>
						<para>     <literal>remote_maxjitter</literal>  Remote calculated jitter (maximum)</para>
						<para>     <literal>remote_minjitter</literal>  Remote calculated jitter (minimum)</para>
						<para>     <literal>remote_normdevjitter</literal>Remote calculated jitter (normal deviation)</para>
						<para>     <literal>remote_stdevjitter</literal>Remote calculated jitter (standard deviation)</para>
						<para>     <literal>remote_count</literal>      Number of transmitted packets</para>
						<para>     <literal>remote_ssrc</literal>       Remote SSRC (stream ID)</para>
						<para>     <literal>remote_lostpackets</literal>Remote lost packets</para>
						<para>     <literal>remote_jitter</literal>     Remote reported jitter</para>
						<para>     <literal>remote_maxjitter</literal>  Remote calculated jitter (maximum)</para>
						<para>     <literal>remote_minjitter</literal>  Remote calculated jitter (minimum)</para>
						<para>     <literal>remote_normdevjitter</literal>Remote calculated jitter (normal deviation)</para>
						<para>     <literal>remote_stdevjitter</literal>Remote calculated jitter (standard deviation)</para>
						<para>     <literal>remote_count</literal>      Number of transmitted packets</para>
						<para>     <literal>rtt</literal>               Round trip time</para>
						<para>     <literal>maxrtt</literal>            Round trip time (maximum)</para>
						<para>     <literal>minrtt</literal>            Round trip time (minimum)</para>
						<para>     <literal>normdevrtt</literal>        Round trip time (normal deviation)</para>
						<para>     <literal>stdevrtt</literal>          Round trip time (standard deviation)</para>
						<para>     <literal>all</literal>               All statistics (in a form suited to logging,
						but not for parsing)</para>
					</enum>
					<enum name="rtpdest">
						<para>R/O Get remote RTP destination information.</para>
						<para>   This option takes one additional argument:</para>
						<para>    Argument 1:</para>
						<para>     <literal>audio</literal>             Get audio destination</para>
						<para>     <literal>video</literal>             Get video destination</para>
						<para>     <literal>text</literal>              Get text destination</para>
					</enum>
				</enumlist>
				<para><emphasis>chan_iax2</emphasis> provides the following additional options:</para>
				<enumlist>
					<enum name="peerip">
						<para>R/O Get the peer's ip address.</para>
					</enum>
					<enum name="peername">
						<para>R/O Get the peer's username.</para>
					</enum>
				</enumlist>
				<para><emphasis>chan_dahdi</emphasis> provides the following additional options:</para>
				<enumlist>
					<enum name="keypad_digits">
						<para>R/O PRI Keypad digits that came in with the SETUP message.</para>
					</enum>
					<enum name="reversecharge">
						<para>R/O PRI Reverse Charging Indication, one of:</para>
						<enumlist>
							<enum name="-1"> <para>None</para></enum>
							<enum name=" 1"> <para>Reverse Charging Requested</para></enum>
						</enumlist>
					</enum>
					<enum name="no_media_path">
						<para>R/O PRI Nonzero if the channel has no B channel.
						The channel is either on hold or a call waiting call.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Gets/sets various pieces of information about the channel, additional <replaceable>item</replaceable> may
			be available from the channel driver; see its documentation for details. Any <replaceable>item</replaceable>
			requested that is not available on the current channel will return an empty string.</para>
		</description>
	</function>
 ***/

#define locked_copy_string(chan, dest, source, len) \
	do { \
		ast_channel_lock(chan); \
		ast_copy_string(dest, source, len); \
		ast_channel_unlock(chan); \
	} while (0)
#define locked_string_field_set(chan, field, source) \
	do { \
		ast_channel_lock(chan); \
		ast_string_field_set(chan, field, source); \
		ast_channel_unlock(chan); \
	} while (0)

static const char * const transfercapability_table[0x20] = {
	"SPEECH", "UNK", "UNK", "UNK", "UNK", "UNK", "UNK", "UNK",
	"DIGITAL", "RESTRICTED_DIGITAL", "UNK", "UNK", "UNK", "UNK", "UNK", "UNK",
	"3K1AUDIO", "DIGITAL_W_TONES", "UNK", "UNK", "UNK", "UNK", "UNK", "UNK",
	"VIDEO", "UNK", "UNK", "UNK", "UNK", "UNK", "UNK", "UNK", };

static int func_channel_read(struct ast_channel *chan, const char *function,
			     char *data, char *buf, size_t len)
{
	int ret = 0;
	char tmp[512];
	struct ast_format_cap *tmpcap;

	if (!strcasecmp(data, "audionativeformat")) {
		if ((tmpcap = ast_format_cap_get_type(chan->nativeformats, AST_FORMAT_TYPE_AUDIO))) {
			ast_copy_string(buf, ast_getformatname_multiple(tmp, sizeof(tmp), tmpcap), len);
			tmpcap = ast_format_cap_destroy(tmpcap);
		}
	} else if (!strcasecmp(data, "videonativeformat")) {
		if ((tmpcap = ast_format_cap_get_type(chan->nativeformats, AST_FORMAT_TYPE_VIDEO))) {
			ast_copy_string(buf, ast_getformatname_multiple(tmp, sizeof(tmp), tmpcap), len);
			tmpcap = ast_format_cap_destroy(tmpcap);
		}
	} else if (!strcasecmp(data, "audioreadformat")) {
		ast_copy_string(buf, ast_getformatname(&chan->readformat), len);
	} else if (!strcasecmp(data, "audiowriteformat")) {
		ast_copy_string(buf, ast_getformatname(&chan->writeformat), len);
#ifdef CHANNEL_TRACE
	} else if (!strcasecmp(data, "trace")) {
		ast_channel_lock(chan);
		ast_copy_string(buf, ast_channel_trace_is_enabled(chan) ? "1" : "0", len);
		ast_channel_unlock(chan);
#endif
	} else if (!strcasecmp(data, "tonezone") && chan->zone)
		locked_copy_string(chan, buf, chan->zone->country, len);
	else if (!strcasecmp(data, "language"))
		locked_copy_string(chan, buf, chan->language, len);
	else if (!strcasecmp(data, "musicclass"))
		locked_copy_string(chan, buf, chan->musicclass, len);
	else if (!strcasecmp(data, "name")) {
		locked_copy_string(chan, buf, chan->name, len);
	} else if (!strcasecmp(data, "parkinglot"))
		locked_copy_string(chan, buf, chan->parkinglot, len);
	else if (!strcasecmp(data, "state"))
		locked_copy_string(chan, buf, ast_state2str(chan->_state), len);
	else if (!strcasecmp(data, "channeltype"))
		locked_copy_string(chan, buf, chan->tech->type, len);
	else if (!strcasecmp(data, "accountcode"))
		locked_copy_string(chan, buf, chan->accountcode, len);
	else if (!strcasecmp(data, "checkhangup")) {
		ast_channel_lock(chan);
		ast_copy_string(buf, ast_check_hangup(chan) ? "1" : "0", len);
		ast_channel_unlock(chan);
	} else if (!strcasecmp(data, "peeraccount"))
		locked_copy_string(chan, buf, chan->peeraccount, len);
	else if (!strcasecmp(data, "hangupsource"))
		locked_copy_string(chan, buf, chan->hangupsource, len);
	else if (!strcasecmp(data, "appname") && chan->appl)
		locked_copy_string(chan, buf, chan->appl, len);
	else if (!strcasecmp(data, "appdata") && chan->data)
		locked_copy_string(chan, buf, chan->data, len);
	else if (!strcasecmp(data, "exten") && chan->data)
		locked_copy_string(chan, buf, chan->exten, len);
	else if (!strcasecmp(data, "context") && chan->data)
		locked_copy_string(chan, buf, chan->context, len);
	else if (!strcasecmp(data, "userfield") && chan->data)
		locked_copy_string(chan, buf, chan->userfield, len);
	else if (!strcasecmp(data, "channame") && chan->data)
		locked_copy_string(chan, buf, chan->name, len);
	else if (!strcasecmp(data, "linkedid")) {
		ast_channel_lock(chan);
		if (ast_strlen_zero(chan->linkedid)) {
			/* fall back on the channel's uniqueid if linkedid is unset */
			ast_copy_string(buf, chan->uniqueid, len);
		}
		else {
			ast_copy_string(buf, chan->linkedid, len);
		}
		ast_channel_unlock(chan);
	} else if (!strcasecmp(data, "peer")) {
		struct ast_channel *p;
		ast_channel_lock(chan);
		p = ast_bridged_channel(chan);
		if (p || chan->tech || chan->cdr) /* dummy channel? if so, we hid the peer name in the language */
			ast_copy_string(buf, (p ? p->name : ""), len);
		else {
			/* a dummy channel can still pass along bridged peer info via
                           the BRIDGEPEER variable */
			const char *pname = pbx_builtin_getvar_helper(chan, "BRIDGEPEER");
			if (!ast_strlen_zero(pname))
				ast_copy_string(buf, pname, len); /* a horrible kludge, but... how else? */
			else
				buf[0] = 0;
		}
		ast_channel_unlock(chan);
	} else if (!strcasecmp(data, "uniqueid")) {
		locked_copy_string(chan, buf, chan->uniqueid, len);
	} else if (!strcasecmp(data, "transfercapability")) {
		locked_copy_string(chan, buf, transfercapability_table[chan->transfercapability & 0x1f], len);
	} else if (!strcasecmp(data, "callgroup")) {
		char groupbuf[256];
		locked_copy_string(chan, buf,  ast_print_group(groupbuf, sizeof(groupbuf), chan->callgroup), len);
	} else if (!strcasecmp(data, "amaflags")) {
		char amabuf[256];
		snprintf(amabuf,sizeof(amabuf), "%d", chan->amaflags);
		locked_copy_string(chan, buf, amabuf, len);
	} else if (!strncasecmp(data, "secure_bridge_", 14)) {
		struct ast_datastore *ds;
		ast_channel_lock(chan);
		if ((ds = ast_channel_datastore_find(chan, &secure_call_info, NULL))) {
			struct ast_secure_call_store *encrypt = ds->data;
			if (!strcasecmp(data, "secure_bridge_signaling")) {
				snprintf(buf, len, "%s", encrypt->signaling ? "1" : "");
			} else if (!strcasecmp(data, "secure_bridge_media")) {
				snprintf(buf, len, "%s", encrypt->media ? "1" : "");
			}
		}
		ast_channel_unlock(chan);
	} else if (!chan->tech || !chan->tech->func_channel_read || chan->tech->func_channel_read(chan, function, data, buf, len)) {
		ast_log(LOG_WARNING, "Unknown or unavailable item requested: '%s'\n", data);
		ret = -1;
	}

	return ret;
}

static int func_channel_write_real(struct ast_channel *chan, const char *function,
			      char *data, const char *value)
{
	int ret = 0;
	signed char gainset;

	if (!strcasecmp(data, "language"))
		locked_string_field_set(chan, language, value);
	else if (!strcasecmp(data, "parkinglot"))
		locked_string_field_set(chan, parkinglot, value);
	else if (!strcasecmp(data, "musicclass"))
		locked_string_field_set(chan, musicclass, value);
	else if (!strcasecmp(data, "accountcode"))
		locked_string_field_set(chan, accountcode, value);
	else if (!strcasecmp(data, "userfield"))
		locked_string_field_set(chan, userfield, value);
	else if (!strcasecmp(data, "amaflags")) {
		ast_channel_lock(chan);
		if(isdigit(*value)) {
			sscanf(value, "%30d", &chan->amaflags);
		} else if (!strcasecmp(value,"OMIT")){
			chan->amaflags = 1;
		} else if (!strcasecmp(value,"BILLING")){
			chan->amaflags = 2;
		} else if (!strcasecmp(value,"DOCUMENTATION")){
			chan->amaflags = 3;
		}
		ast_channel_unlock(chan);
	} else if (!strcasecmp(data, "peeraccount"))
		locked_string_field_set(chan, peeraccount, value);
	else if (!strcasecmp(data, "hangupsource"))
		/* XXX - should we be forcing this here? */
		ast_set_hangupsource(chan, value, 0);
#ifdef CHANNEL_TRACE
	else if (!strcasecmp(data, "trace")) {
		ast_channel_lock(chan);
		if (ast_true(value)) 
			ret = ast_channel_trace_enable(chan);
		else if (ast_false(value))
			ret = ast_channel_trace_disable(chan);
		else {
			ret = -1;
			ast_log(LOG_WARNING, "Invalid value for CHANNEL(trace).");
		}
		ast_channel_unlock(chan);
	}
#endif
	else if (!strcasecmp(data, "tonezone")) {
		struct ast_tone_zone *new_zone;
		if (!(new_zone = ast_get_indication_zone(value))) {
			ast_log(LOG_ERROR, "Unknown country code '%s' for tonezone. Check indications.conf for available country codes.\n", value);
			ret = -1;	
		} else {
			ast_channel_lock(chan);
			if (chan->zone) {
				chan->zone = ast_tone_zone_unref(chan->zone);
			}
			chan->zone = ast_tone_zone_ref(new_zone);
			ast_channel_unlock(chan);
			new_zone = ast_tone_zone_unref(new_zone);
		}
	} else if (!strcasecmp(data, "callgroup"))
		chan->callgroup = ast_get_group(value);
	else if (!strcasecmp(data, "txgain")) {
		sscanf(value, "%4hhd", &gainset);
		ast_channel_setoption(chan, AST_OPTION_TXGAIN, &gainset, sizeof(gainset), 0);
	} else if (!strcasecmp(data, "rxgain")) {
		sscanf(value, "%4hhd", &gainset);
		ast_channel_setoption(chan, AST_OPTION_RXGAIN, &gainset, sizeof(gainset), 0);
	} else if (!strcasecmp(data, "transfercapability")) {
		unsigned short i;
		for (i = 0; i < 0x20; i++) {
			if (!strcasecmp(transfercapability_table[i], value) && strcmp(value, "UNK")) {
				chan->transfercapability = i;
				break;
			}
		}
	} else if (!strncasecmp(data, "secure_bridge_", 14)) {
		struct ast_datastore *ds;
		struct ast_secure_call_store *store;

		if (!chan || !value) {
			return -1;
		}

		ast_channel_lock(chan);
		if (!(ds = ast_channel_datastore_find(chan, &secure_call_info, NULL))) {
			if (!(ds = ast_datastore_alloc(&secure_call_info, NULL))) {
				ast_channel_unlock(chan);
				return -1;
			}
			if (!(store = ast_calloc(1, sizeof(*store)))) {
				ast_channel_unlock(chan);
				ast_free(ds);
				return -1;
			}
			ds->data = store;
			ast_channel_datastore_add(chan, ds);
		} else {
			store = ds->data;
		}
		ast_channel_unlock(chan);

		if (!strcasecmp(data, "secure_bridge_signaling")) {
			store->signaling = ast_true(value) ? 1 : 0;
		} else if (!strcasecmp(data, "secure_bridge_media")) {
			store->media = ast_true(value) ? 1 : 0;
		}
	} else if (!chan->tech->func_channel_write
		 || chan->tech->func_channel_write(chan, function, data, value)) {
		ast_log(LOG_WARNING, "Unknown or unavailable item requested: '%s'\n",
				data);
		ret = -1;
	}

	return ret;
}

static int func_channel_write(struct ast_channel *chan, const char *function, char *data, const char *value)
{
	int res;
	ast_chan_write_info_t write_info = {
		.version = AST_CHAN_WRITE_INFO_T_VERSION,
		.write_fn = func_channel_write_real,
		.chan = chan,
		.function = function,
		.data = data,
		.value = value,
	};

	res = func_channel_write_real(chan, function, data, value);
	ast_channel_setoption(chan, AST_OPTION_CHANNEL_WRITE, &write_info, sizeof(write_info), 0);

	return res;
}

static struct ast_custom_function channel_function = {
	.name = "CHANNEL",
	.read = func_channel_read,
	.write = func_channel_write,
};

static int func_channels_read(struct ast_channel *chan, const char *function, char *data, char *buf, size_t maxlen)
{
	struct ast_channel *c = NULL;
	regex_t re;
	int res;
	size_t buflen = 0;
	struct ast_channel_iterator *iter;

	buf[0] = '\0';

	if (!ast_strlen_zero(data)) {
		if ((res = regcomp(&re, data, REG_EXTENDED | REG_ICASE | REG_NOSUB))) {
			regerror(res, &re, buf, maxlen);
			ast_log(LOG_WARNING, "Error compiling regular expression for %s(%s): %s\n", function, data, buf);
			return -1;
		}
	}

	if (!(iter = ast_channel_iterator_all_new())) {
		if (!ast_strlen_zero(data)) {
			regfree(&re);
		}
		return -1;
	}

	while ((c = ast_channel_iterator_next(iter))) {
		ast_channel_lock(c);
		if (ast_strlen_zero(data) || regexec(&re, c->name, 0, NULL, 0) == 0) {
			size_t namelen = strlen(c->name);
			if (buflen + namelen + (ast_strlen_zero(buf) ? 0 : 1) + 1 < maxlen) {
				if (!ast_strlen_zero(buf)) {
					strcat(buf, " ");
					buflen++;
				}
				strcat(buf, c->name);
				buflen += namelen;
			} else {
				ast_log(LOG_WARNING, "Number of channels exceeds the available buffer space.  Output will be truncated!\n");
			}
		}
		ast_channel_unlock(c);
		c = ast_channel_unref(c);
	}

	ast_channel_iterator_destroy(iter);

	if (!ast_strlen_zero(data)) {
		regfree(&re);
	}

	return 0;
}

static struct ast_custom_function channels_function = {
	.name = "CHANNELS",
	.read = func_channels_read,
};

static int func_mchan_read(struct ast_channel *chan, const char *function,
			     char *data, struct ast_str **buf, ssize_t len)
{
	struct ast_channel *mchan = ast_channel_get_by_name(chan->linkedid);
	char *template = alloca(4 + strlen(data));
	sprintf(template, "${%s}", data); /* SAFE */
	ast_str_substitute_variables(buf, len, mchan ? mchan : chan, template);
	if (mchan) {
		ast_channel_unref(mchan);
	}
	return 0;
}

static int func_mchan_write(struct ast_channel *chan, const char *function,
			      char *data, const char *value)
{
	struct ast_channel *mchan = ast_channel_get_by_name(chan->linkedid);
	pbx_builtin_setvar_helper(mchan ? mchan : chan, data, value);
	if (mchan) {
		ast_channel_unref(mchan);
	}
	return 0;
}

static struct ast_custom_function mchan_function = {
	.name = "MASTER_CHANNEL",
	.read2 = func_mchan_read,
	.write = func_mchan_write,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&channel_function);
	res |= ast_custom_function_unregister(&channels_function);
	res |= ast_custom_function_unregister(&mchan_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&channel_function);
	res |= ast_custom_function_register(&channels_function);
	res |= ast_custom_function_register(&mchan_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Channel information dialplan functions");
