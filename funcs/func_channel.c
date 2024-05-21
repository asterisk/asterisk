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
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <regex.h>
#include <ctype.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/indications.h"
#include "asterisk/stringfields.h"
#include "asterisk/global_datastores.h"
#include "asterisk/bridge_basic.h"
#include "asterisk/bridge_after.h"
#include "asterisk/max_forwards.h"

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
	<function name="CHANNEL_EXISTS" language="en_US">
		<since>
			<version>16.22.0</version>
			<version>18.8.0</version>
			<version>19.0.0</version>
		</since>
		<synopsis>
			Checks if the specified channel exists.
		</synopsis>
		<syntax>
			<parameter name="name_or_uid" required="true">
				<para>The name or unique ID of the channel to check.</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns 1 if the channel <replaceable>name_or_uid</replaceable> exists, 0 if not.</para>
		</description>
	</function>
	<function name="MASTER_CHANNEL" language="en_US">
		<synopsis>
			Gets or sets variables on the master channel
		</synopsis>
		<description>
			<para>Allows access to the oldest channel associated with the current
			channel if it still exists.  If the channel is the master channel or
			the master channel no longer exists then access local channel variables
			instead.  In other words, the master channel is the channel identified by
			the channel's linkedid.</para>
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
					<enum name="amaflags">
						<para>R/W the Automatic Message Accounting (AMA) flags on the channel.
						When read from a channel, the integer value will always be returned.
						When written to a channel, both the string format or integer value
						is accepted.</para>
						<enumlist>
							<enum name="1"><para><literal>OMIT</literal></para></enum>
							<enum name="2"><para><literal>BILLING</literal></para></enum>
							<enum name="3"><para><literal>DOCUMENTATION</literal></para></enum>
						</enumlist>
					</enum>
					<enum name="accountcode">
						<para>R/W the channel's account code.</para>
					</enum>
					<enum name="audioreadformat">
						<para>R/O format currently being read.</para>
					</enum>
					<enum name="audionativeformat">
						<para>R/O format used natively for audio.</para>
					</enum>
					<enum name="audiowriteformat">
						<para>R/O format currently being written.</para>
					</enum>
					<enum name="dtmf_features">
						<para>R/W The channel's DTMF bridge features.
						May include one or more of 'T' 'K' 'H' 'W' and 'X' in a similar manner to options
						in the <literal>Dial</literal> application. When setting it, the features string
						must be all upper case.</para>
					</enum>
					<enum name="callgroup">
						<para>R/W numeric call pickup groups that this channel is a member.</para>
					</enum>
					<enum name="pickupgroup">
						<para>R/W numeric call pickup groups this channel can pickup.</para>
					</enum>
					<enum name="namedcallgroup">
						<para>R/W named call pickup groups that this channel is a member.</para>
					</enum>
					<enum name="namedpickupgroup">
						<para>R/W named call pickup groups this channel can pickup.</para>
					</enum>
					<enum name="channeltype">
						<para>R/O technology used for channel.</para>
					</enum>
					<enum name="checkhangup">
						<para>R/O Whether the channel is hanging up (1/0)</para>
					</enum>
					<enum name="digitdetect">
						<para>R/W Enable or disable DTMF detection on channel drivers that support it.</para>
						<para>If set on a DAHDI channel, this will only disable DTMF detection, not pulse dialing detection.
						To disable pulse dialing, use the <literal>dialmode</literal> option.</para>
						<para>On DAHDI channels, this will disable DSP if it is not needed for anything else.
						This will prevent DTMF detection regardless of the <literal>dialmode</literal> setting.</para>
					</enum>
					<enum name="faxdetect">
						<para>R/W Enable or disable fax detection on channel drivers that support it.</para>
					</enum>
					<enum name="after_bridge_goto">
						<para>R/W the parseable goto string indicating where the channel is
						expected to return to in the PBX after exiting the next bridge it joins
						on the condition that it doesn't hang up. The parseable goto string uses
						the same syntax as the <literal>Goto</literal> application.</para>
					</enum>
					<enum name="hangup_handler_pop">
						<para>W/O Replace the most recently added hangup handler
						with a new hangup handler on the channel if supplied.  The
						assigned string is passed to the Gosub application when
						the channel is hung up.  Any optionally omitted context
						and exten are supplied by the channel pushing the handler
						before it is pushed.</para>
					</enum>
					<enum name="hangup_handler_push">
						<para>W/O Push a hangup handler onto the channel hangup
						handler stack.  The assigned string is passed to the
						Gosub application when the channel is hung up.  Any
						optionally omitted context and exten are supplied by the
						channel pushing the handler before it is pushed.</para>
					</enum>
					<enum name="hangup_handler_wipe">
						<para>W/O Wipe the entire hangup handler stack and replace
						with a new hangup handler on the channel if supplied.  The
						assigned string is passed to the Gosub application when
						the channel is hung up.  Any optionally omitted context
						and exten are supplied by the channel pushing the handler
						before it is pushed.</para>
					</enum>
					<enum name="onhold">
						<para>R/O Whether or not the channel is onhold. (1/0)</para>
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
					<enum name="relaxdtmf">
						<para>W/O Enable or disable relaxed DTMF detection for channel drivers that support it,
						overriding any setting previously defaulted by the channel driver.</para>
					</enum>
					<enum name="rxgain">
						<para>R/W set rxgain level on channel drivers that support it.</para>
					</enum>
					<enum name="secure_bridge_signaling">
						<para>Whether or not channels bridged to this channel require secure signaling (1/0)</para>
					</enum>
					<enum name="secure_bridge_media">
						<para>Whether or not channels bridged to this channel require secure media (1/0)</para>
					</enum>
					<enum name="state">
						<para>R/O state of the channel</para>
					</enum>
					<enum name="tdd">
						<para>R/W Enable or disable TDD mode on channel drivers that support it.</para>
						<para>When reading this option, 1 indicates TDD mode enabled, 0 indicates TDD mode disabled,
						and <literal>mate</literal> indicates TDD mate mode.</para>
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
					<enum name="hangupsource">
						<para>R/W returns the channel responsible for hangup.</para>
					</enum>
					<enum name="appname">
						<para>R/O returns the internal application name.</para>
					</enum>
					<enum name="appdata">
						<para>R/O returns the application data if available.</para>
					</enum>
					<enum name="exten">
						<para>R/O returns the extension for an outbound channel.</para>
					</enum>
					<enum name="context">
						<para>R/O returns the context for an outbound channel.</para>
					</enum>
					<enum name="lastexten">
						<para>R/O returns the last unique extension for an outbound channel.</para>
					</enum>
					<enum name="lastcontext">
						<para>R/O returns the last unique context for an outbound channel.</para>
					</enum>
					<enum name="channame">
						<para>R/O returns the channel name for an outbound channel.</para>
					</enum>
					<enum name="uniqueid">
						<para>R/O returns the channel uniqueid.</para>
					</enum>
					<enum name="linkedid">
						<para>R/O returns the linkedid if available, otherwise returns the uniqueid.</para>
					</enum>
					<enum name="tenantid">
						<para>R/W The channel tenantid.</para>
					</enum>
					<enum name="max_forwards">
						<para>R/W The maximum number of forwards allowed.</para>
					</enum>
					<enum name="callid">
						<para>R/O Call identifier log tag associated with the channel
						e.g., <literal>[C-00000000]</literal>.</para>
					</enum>
				</enumlist>
				<xi:include xpointer="xpointer(/docs/info[@name='CHANNEL'])" />
			</parameter>
		</syntax>
		<description>
			<para>Gets/sets various pieces of information about the channel, additional <replaceable>item</replaceable> may
			be available from the channel driver; see its documentation for details. Any <replaceable>item</replaceable>
			requested that is not available on the current channel will return an empty string.</para>
			<example title="Standard CHANNEL item examples">
				; Push a hangup handler subroutine existing at dialplan
				; location default,s,1 onto the current channel
				same => n,Set(CHANNEL(hangup_handler_push)=default,s,1)

				; Set the current tonezone to Germany (de)
				same => n,Set(CHANNEL(tonezone)=de)

				; Set the allowed maximum number of forwarding attempts
				same => n,Set(CHANNEL(max_forwards)=10)

				; If this channel is ejected from its next bridge, and if
				; the channel is not hung up, begin executing dialplan at
				; location default,after-bridge,1
				same => n,Set(CHANNEL(after_bridge_goto)=default,after-bridge,1)

				; Log the current state of the channel
				same => n,Log(NOTICE, This channel is: ${CHANNEL(state)})
			</example>
			<xi:include xpointer="xpointer(/docs/info[@name='CHANNEL_EXAMPLES'])" />
			<para>The following channel variables are available as special built-in
			dialplan channel variables. These variables cannot be set or modified
			and are read-only.</para>
			<variablelist>
				<variable name="CALLINGPRES">
					<para>Caller ID presentation for incoming calls (PRI channels)</para>
				</variable>
				<variable name="CALLINGANI2">
					<para>Caller ANI2 (PRI channels)</para>
				</variable>
				<variable name="CALLINGTON">
					<para>Caller Type of Number (PRI channels)</para>
				</variable>
				<variable name="CALLINGTNS">
					<para>Transit Network Selector (PRI channels)</para>
				</variable>
				<variable name="EXTEN">
					<para>Current extension</para>
				</variable>
				<variable name="CONTEXT">
					<para>Current context</para>
				</variable>
				<variable name="PRIORITY">
					<para>Current priority</para>
				</variable>
				<variable name="CHANNEL">
					<para>Current channel name</para>
				</variable>
				<variable name="UNIQUEID">
					<para>Current call unique identifier</para>
				</variable>
				<variable name="HANGUPCAUSE">
					<para>Asterisk cause of hangup (inbound/outbound)</para>
				</variable>
			</variablelist>
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
		ast_channel_##field##_set(chan, source); \
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
	struct ast_format_cap *tmpcap;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", function);
		return -1;
	}

	if (!strcasecmp(data, "audionativeformat")) {
		tmpcap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (tmpcap) {
			struct ast_str *codec_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);

			ast_channel_lock(chan);
			ast_format_cap_append_from_cap(tmpcap, ast_channel_nativeformats(chan), AST_MEDIA_TYPE_AUDIO);
			ast_channel_unlock(chan);
			ast_copy_string(buf, ast_format_cap_get_names(tmpcap, &codec_buf), len);
			ao2_ref(tmpcap, -1);
		}
	} else if (!strcasecmp(data, "videonativeformat")) {
		tmpcap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (tmpcap) {
			struct ast_str *codec_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);

			ast_channel_lock(chan);
			ast_format_cap_append_from_cap(tmpcap, ast_channel_nativeformats(chan), AST_MEDIA_TYPE_VIDEO);
			ast_channel_unlock(chan);
			ast_copy_string(buf, ast_format_cap_get_names(tmpcap, &codec_buf), len);
			ao2_ref(tmpcap, -1);
		}
	} else if (!strcasecmp(data, "audioreadformat")) {
		locked_copy_string(chan, buf, ast_format_get_name(ast_channel_readformat(chan)), len);
	} else if (!strcasecmp(data, "audiowriteformat")) {
		locked_copy_string(chan, buf, ast_format_get_name(ast_channel_writeformat(chan)), len);
	} else if (!strcasecmp(data, "tonezone") && ast_channel_zone(chan)) {
		locked_copy_string(chan, buf, ast_channel_zone(chan)->country, len);
	} else if (!strcasecmp(data, "dtmf_features")) {
		if (ast_bridge_features_ds_get_string(chan, buf, len)) {
			buf[0] = '\0';
		}
	} else if (!strcasecmp(data, "language"))
		locked_copy_string(chan, buf, ast_channel_language(chan), len);
	else if (!strcasecmp(data, "musicclass"))
		locked_copy_string(chan, buf, ast_channel_musicclass(chan), len);
	else if (!strcasecmp(data, "name")) {
		locked_copy_string(chan, buf, ast_channel_name(chan), len);
	} else if (!strcasecmp(data, "parkinglot"))
		locked_copy_string(chan, buf, ast_channel_parkinglot(chan), len);
	else if (!strcasecmp(data, "state"))
		locked_copy_string(chan, buf, ast_state2str(ast_channel_state(chan)), len);
	else if (!strcasecmp(data, "onhold")) {
		locked_copy_string(chan, buf,
			ast_channel_hold_state(chan) == AST_CONTROL_HOLD ? "1" : "0", len);
	} else if (!strcasecmp(data, "channeltype"))
		locked_copy_string(chan, buf, ast_channel_tech(chan)->type, len);
	else if (!strcasecmp(data, "accountcode"))
		locked_copy_string(chan, buf, ast_channel_accountcode(chan), len);
	else if (!strcasecmp(data, "checkhangup")) {
		locked_copy_string(chan, buf, ast_check_hangup(chan) ? "1" : "0", len);
	} else if (!strcasecmp(data, "peeraccount"))
		locked_copy_string(chan, buf, ast_channel_peeraccount(chan), len);
	else if (!strcasecmp(data, "hangupsource"))
		locked_copy_string(chan, buf, ast_channel_hangupsource(chan), len);
	else if (!strcasecmp(data, "appname") && ast_channel_appl(chan))
		locked_copy_string(chan, buf, ast_channel_appl(chan), len);
	else if (!strcasecmp(data, "appdata") && ast_channel_data(chan))
		locked_copy_string(chan, buf, ast_channel_data(chan), len);
	else if (!strcasecmp(data, "exten"))
		locked_copy_string(chan, buf, ast_channel_exten(chan), len);
	else if (!strcasecmp(data, "context"))
		locked_copy_string(chan, buf, ast_channel_context(chan), len);
	else if (!strcasecmp(data, "lastexten"))
		locked_copy_string(chan, buf, ast_channel_lastexten(chan), len);
	else if (!strcasecmp(data, "lastcontext"))
		locked_copy_string(chan, buf, ast_channel_lastcontext(chan), len);
	else if (!strcasecmp(data, "userfield"))
		locked_copy_string(chan, buf, ast_channel_userfield(chan), len);
	else if (!strcasecmp(data, "channame"))
		locked_copy_string(chan, buf, ast_channel_name(chan), len);
	else if (!strcasecmp(data, "linkedid")) {
		ast_channel_lock(chan);
		if (ast_strlen_zero(ast_channel_linkedid(chan))) {
			/* fall back on the channel's uniqueid if linkedid is unset */
			ast_copy_string(buf, ast_channel_uniqueid(chan), len);
		}
		else {
			ast_copy_string(buf, ast_channel_linkedid(chan), len);
		}
		ast_channel_unlock(chan);
	} else if (!strcasecmp(data, "peer")) {
		struct ast_channel *peer;

		peer = ast_channel_bridge_peer(chan);
		if (peer) {
			/* Only real channels could have a bridge peer this way. */
			ast_channel_lock(peer);
			ast_copy_string(buf, ast_channel_name(peer), len);
			ast_channel_unlock(peer);
			ast_channel_unref(peer);
		} else {
			buf[0] = '\0';
			ast_channel_lock(chan);
			if (!ast_channel_tech(chan)) {
				const char *pname;

				/*
				 * A dummy channel can still pass along bridged peer info
				 * via the BRIDGEPEER variable.
				 *
				 * A horrible kludge, but... how else?
				 */
				pname = pbx_builtin_getvar_helper(chan, "BRIDGEPEER");
				if (!ast_strlen_zero(pname)) {
					ast_copy_string(buf, pname, len);
				}
			}
			ast_channel_unlock(chan);
		}
	} else if (!strcasecmp(data, "uniqueid")) {
		locked_copy_string(chan, buf, ast_channel_uniqueid(chan), len);
	} else if (!strcasecmp(data, "transfercapability")) {
		locked_copy_string(chan, buf, transfercapability_table[ast_channel_transfercapability(chan) & 0x1f], len);
	} else if (!strcasecmp(data, "callgroup")) {
		char groupbuf[256];

		locked_copy_string(chan, buf,  ast_print_group(groupbuf, sizeof(groupbuf), ast_channel_callgroup(chan)), len);
	} else if (!strcasecmp(data, "pickupgroup")) {
		char groupbuf[256];

		locked_copy_string(chan, buf,  ast_print_group(groupbuf, sizeof(groupbuf), ast_channel_pickupgroup(chan)), len);
	} else if (!strcasecmp(data, "namedcallgroup")) {
		struct ast_str *tmp_str = ast_str_alloca(1024);

		locked_copy_string(chan, buf,  ast_print_namedgroups(&tmp_str, ast_channel_named_callgroups(chan)), len);
	} else if (!strcasecmp(data, "namedpickupgroup")) {
		struct ast_str *tmp_str = ast_str_alloca(1024);

		locked_copy_string(chan, buf,  ast_print_namedgroups(&tmp_str, ast_channel_named_pickupgroups(chan)), len);
	} else if (!strcasecmp(data, "after_bridge_goto")) {
		ast_bridge_read_after_goto(chan, buf, len);
	} else if (!strcasecmp(data, "amaflags")) {
		ast_channel_lock(chan);
		snprintf(buf, len, "%u", ast_channel_amaflags(chan));
		ast_channel_unlock(chan);
	} else if (!strncasecmp(data, "secure_bridge_", 14)) {
		struct ast_datastore *ds;

		buf[0] = '\0';
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
	} else if (!strcasecmp(data, "max_forwards")) {
		ast_channel_lock(chan);
		snprintf(buf, len, "%d", ast_max_forwards_get(chan));
		ast_channel_unlock(chan);
	} else if (!strcasecmp(data, "callid")) {
		ast_callid callid;

		buf[0] = '\0';
		ast_channel_lock(chan);
		callid = ast_channel_callid(chan);
		if (callid) {
			ast_callid_strnprint(buf, len, callid);
		}
		ast_channel_unlock(chan);
	} else if (!strcasecmp(data, "tdd")) {
		char status;
		int status_size = (int) sizeof(status);
		ret = ast_channel_queryoption(chan, AST_OPTION_TDD, &status, &status_size, 0);
		if (!ret) {
			ast_copy_string(buf, status == 2 ? "mate" : status ? "1" : "0", len);
		}
	} else if (!strcasecmp(data, "digitdetect")) {
		char status;
		int status_size = (int) sizeof(status);
		ret = ast_channel_queryoption(chan, AST_OPTION_DIGIT_DETECT, &status, &status_size, 0);
		if (!ret) {
			ast_copy_string(buf, status ? "1" : "0", len);
		}
	} else if (!strcasecmp(data, "faxdetect")) {
		char status;
		int status_size = (int) sizeof(status);
		ret = ast_channel_queryoption(chan, AST_OPTION_FAX_DETECT, &status, &status_size, 0);
		if (!ret) {
			ast_copy_string(buf, status ? "1" : "0", len);
		}
	} else if (!strcasecmp(data, "device_name")) {
		ret = ast_channel_get_device_name(chan, buf, len);
	} else if (!strcasecmp(data, "tenantid")) {
		locked_copy_string(chan, buf, ast_channel_tenantid(chan), len);
	} else if (!ast_channel_tech(chan) || !ast_channel_tech(chan)->func_channel_read || ast_channel_tech(chan)->func_channel_read(chan, function, data, buf, len)) {
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
	else if (!strcasecmp(data, "after_bridge_goto")) {
		if (ast_strlen_zero(value)) {
			ast_bridge_discard_after_goto(chan);
		} else {
			ast_bridge_set_after_go_on(chan, ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan), value);
		}
	} else if (!strcasecmp(data, "amaflags")) {
		int amaflags;

		if (isdigit(*value)) {
			if (sscanf(value, "%30d", &amaflags) != 1) {
				amaflags = AST_AMA_NONE;
			}
		} else {
			amaflags = ast_channel_string2amaflag(value);
		}
		ast_channel_lock(chan);
		ast_channel_amaflags_set(chan, amaflags);
		ast_channel_unlock(chan);
	} else if (!strcasecmp(data, "peeraccount"))
		locked_string_field_set(chan, peeraccount, value);
	else if (!strcasecmp(data, "hangupsource"))
		/* XXX - should we be forcing this here? */
		ast_set_hangupsource(chan, value, 0);
	else if (!strcasecmp(data, "tonezone")) {
		struct ast_tone_zone *new_zone;
		if (!(new_zone = ast_get_indication_zone(value))) {
			ast_log(LOG_ERROR, "Unknown country code '%s' for tonezone. Check indications.conf for available country codes.\n", value);
			ret = -1;
		} else {
			ast_channel_lock(chan);
			if (ast_channel_zone(chan)) {
				ast_channel_zone_set(chan, ast_tone_zone_unref(ast_channel_zone(chan)));
			}
			ast_channel_zone_set(chan, ast_tone_zone_ref(new_zone));
			ast_channel_unlock(chan);
			new_zone = ast_tone_zone_unref(new_zone);
		}
	} else if (!strcasecmp(data, "dtmf_features")) {
		ret = ast_bridge_features_ds_set_string(chan, value);
	} else if (!strcasecmp(data, "callgroup")) {
		ast_channel_lock(chan);
		ast_channel_callgroup_set(chan, ast_get_group(value));
		ast_channel_unlock(chan);
	} else if (!strcasecmp(data, "pickupgroup")) {
		ast_channel_lock(chan);
		ast_channel_pickupgroup_set(chan, ast_get_group(value));
		ast_channel_unlock(chan);
	} else if (!strcasecmp(data, "namedcallgroup")) {
		struct ast_namedgroups *groups = ast_get_namedgroups(value);

		ast_channel_lock(chan);
		ast_channel_named_callgroups_set(chan, groups);
		ast_channel_unlock(chan);
		ast_unref_namedgroups(groups);
	} else if (!strcasecmp(data, "namedpickupgroup")) {
		struct ast_namedgroups *groups = ast_get_namedgroups(value);

		ast_channel_lock(chan);
		ast_channel_named_pickupgroups_set(chan, groups);
		ast_channel_unlock(chan);
		ast_unref_namedgroups(groups);
	} else if (!strcasecmp(data, "tdd")) {
		char enabled;
		if (!strcasecmp(value, "mate")) {
			enabled = 2;
		} else {
			enabled = ast_true(value) ? 1 : 0;
		}
		ast_channel_setoption(chan, AST_OPTION_TDD, &enabled, sizeof(enabled), 0);
	} else if (!strcasecmp(data, "relaxdtmf")) {
		char enabled = ast_true(value) ? 1 : 0;
		ast_channel_setoption(chan, AST_OPTION_RELAXDTMF, &enabled, sizeof(enabled), 0);
	} else if (!strcasecmp(data, "txgain")) {
		sscanf(value, "%4hhd", &gainset);
		ast_channel_setoption(chan, AST_OPTION_TXGAIN, &gainset, sizeof(gainset), 0);
	} else if (!strcasecmp(data, "rxgain")) {
		sscanf(value, "%4hhd", &gainset);
		ast_channel_setoption(chan, AST_OPTION_RXGAIN, &gainset, sizeof(gainset), 0);
	} else if (!strcasecmp(data, "digitdetect")) {
		char enabled = ast_true(value) ? 1 : 0;
		ast_channel_setoption(chan, AST_OPTION_DIGIT_DETECT, &enabled, sizeof(enabled), 0);
	} else if (!strcasecmp(data, "faxdetect")) {
		char enabled = ast_true(value) ? 1 : 0;
		ast_channel_setoption(chan, AST_OPTION_FAX_DETECT, &enabled, sizeof(enabled), 0);
	} else if (!strcasecmp(data, "transfercapability")) {
		unsigned short i;

		ast_channel_lock(chan);
		for (i = 0; i < 0x20; i++) {
			if (!strcasecmp(transfercapability_table[i], value) && strcmp(value, "UNK")) {
				ast_channel_transfercapability_set(chan, i);
				break;
			}
		}
		ast_channel_unlock(chan);
	} else if (!strcasecmp(data, "hangup_handler_pop")) {
		/* Pop one hangup handler before pushing the new handler. */
		ast_pbx_hangup_handler_pop(chan);
		ast_pbx_hangup_handler_push(chan, value);
	} else if (!strcasecmp(data, "hangup_handler_push")) {
		ast_pbx_hangup_handler_push(chan, value);
	} else if (!strcasecmp(data, "hangup_handler_wipe")) {
		/* Pop all hangup handlers before pushing the new handler. */
		while (ast_pbx_hangup_handler_pop(chan)) {
		}
		ast_pbx_hangup_handler_push(chan, value);
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

		if (!strcasecmp(data, "secure_bridge_signaling")) {
			store->signaling = ast_true(value) ? 1 : 0;
		} else if (!strcasecmp(data, "secure_bridge_media")) {
			store->media = ast_true(value) ? 1 : 0;
		}
		ast_channel_unlock(chan);
	} else if (!strcasecmp(data, "max_forwards")) {
		int max_forwards;
		if (sscanf(value, "%d", &max_forwards) != 1) {
			ast_log(LOG_WARNING, "Unable to set max forwards to '%s'\n", value);
			ret = -1;
		} else {
			ast_channel_lock(chan);
			ret = ast_max_forwards_set(chan, max_forwards);
			ast_channel_unlock(chan);
		}
	} else if (!strcasecmp(data, "tenantid")) {
		ast_channel_tenantid_set(chan, value);
	} else if (!ast_channel_tech(chan)->func_channel_write
		 || ast_channel_tech(chan)->func_channel_write(chan, function, data, value)) {
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

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", function);
		return -1;
	}

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
		if (ast_strlen_zero(data) || regexec(&re, ast_channel_name(c), 0, NULL, 0) == 0) {
			size_t namelen = strlen(ast_channel_name(c));
			if (buflen + namelen + (ast_strlen_zero(buf) ? 0 : 1) + 1 < maxlen) {
				if (!ast_strlen_zero(buf)) {
					strcat(buf, " ");
					buflen++;
				}
				strcat(buf, ast_channel_name(c));
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

static int func_chan_exists_read(struct ast_channel *chan, const char *function, char *data, char *buf, size_t maxlen)
{
	struct ast_channel *chan_found = NULL;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s: Channel name or unique ID required\n", function);
		return -1;
	}

	chan_found = ast_channel_get_by_name(data);
	snprintf(buf, maxlen, "%d", (chan_found ? 1 : 0));
	if (chan_found) {
		ast_channel_unref(chan_found);
	}
	return 0;
}

static struct ast_custom_function chan_exists_function = {
	.name = "CHANNEL_EXISTS",
	.read = func_chan_exists_read,
};

static int func_mchan_read(struct ast_channel *chan, const char *function,
			     char *data, struct ast_str **buf, ssize_t len)
{
	struct ast_channel *mchan;
	char *template = ast_alloca(4 + strlen(data));

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", function);
		return -1;
	}

	mchan = ast_channel_get_by_name(ast_channel_linkedid(chan));
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
	struct ast_channel *mchan;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", function);
		return -1;
	}

	mchan = ast_channel_get_by_name(ast_channel_linkedid(chan));
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
	res |= ast_custom_function_unregister(&chan_exists_function);
	res |= ast_custom_function_unregister(&mchan_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&channel_function);
	res |= ast_custom_function_register(&channels_function);
	res |= ast_custom_function_register(&chan_exists_function);
	res |= ast_custom_function_register(&mchan_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Channel information dialplan functions");
