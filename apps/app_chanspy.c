/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005 Anthony Minessale II (anthmct@yahoo.com)
 * Copyright (C) 2005 - 2008, Digium, Inc.
 *
 * A license has been granted to Digium (via disclaimer) for the use of
 * this code.
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
 * \brief ChanSpy: Listen in on any channel.
 *
 * \author Anthony Minessale II <anthmct@yahoo.com>
 * \author Joshua Colp <jcolp@digium.com>
 * \author Russell Bryant <russell@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <ctype.h>
#include <errno.h>

#include "asterisk/paths.h" /* use ast_config_AST_MONITOR_DIR */
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/audiohook.h"
#include "asterisk/features.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/say.h"
#include "asterisk/pbx.h"
#include "asterisk/translate.h"
#include "asterisk/manager.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/options.h"
#include "asterisk/autochan.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/json.h"
#include "asterisk/format_cache.h"

#define AST_NAME_STRLEN 256
#define NUM_SPYGROUPS 128

/*** DOCUMENTATION
	<application name="ChanSpy" language="en_US">
		<synopsis>
			Listen to a channel, and optionally whisper into it.
		</synopsis>
		<syntax>
			<parameter name="chanprefix" />
			<parameter name="options">
				<optionlist>
					<option name="b">
						<para>Only spy on channels involved in a bridged call.</para>
					</option>
					<option name="B">
						<para>Instead of whispering on a single channel barge in on both
						channels involved in the call.</para>
					</option>
					<option name="c">
						<argument name="digit" required="true">
							<para>Specify a DTMF digit that can be used to spy on the next available channel.</para>
						</argument>
					</option>
					<option name="d">
						<para>Override the typical numeric DTMF functionality and instead
						use DTMF to switch between spy modes.</para>
						<enumlist>
							<enum name="4">
								<para>spy mode</para>
							</enum>
							<enum name="5">
								<para>whisper mode</para>
							</enum>
							<enum name="6">
								<para>barge mode</para>
							</enum>
						</enumlist>
					</option>
					<option name="e">
						<argument name="ext" required="true" />
						<para>Enable <emphasis>enforced</emphasis> mode, so the spying channel can
						only monitor extensions whose name is in the <replaceable>ext</replaceable> : delimited
						list.</para>
					</option>
					<option name="E">
						<para>Exit when the spied-on channel hangs up.</para>
					</option>
					<option name="g">
						<argument name="grp" required="true">
							<para>Only spy on channels in which one or more of the groups
							listed in <replaceable>grp</replaceable> matches one or more groups from the
							<variable>SPYGROUP</variable> variable set on the channel to be spied upon.</para>
						</argument>
						<note><para>both <replaceable>grp</replaceable> and <variable>SPYGROUP</variable> can contain
						either a single group or a colon-delimited list of groups, such
						as <literal>sales:support:accounting</literal>.</para></note>
					</option>
					<option name="n" argsep="@">
						<para>Say the name of the person being spied on if that person has recorded
						his/her name. If a context is specified, then that voicemail context will
						be searched when retrieving the name, otherwise the <literal>default</literal> context
						be used when searching for the name (i.e. if SIP/1000 is the channel being
						spied on and no mailbox is specified, then <literal>1000</literal> will be used when searching
						for the name).</para>
						<argument name="mailbox" />
						<argument name="context" />
					</option>
					<option name="o">
						<para>Only listen to audio coming from this channel.</para>
					</option>
					<option name="q">
						<para>Don't play a beep when beginning to spy on a channel, or speak the
						selected channel name.</para>
					</option>
					<option name="r">
						<para>Record the session to the monitor spool directory. An optional base for the filename
						may be specified. The default is <literal>chanspy</literal>.</para>
						<argument name="basename" />
					</option>
					<option name="s">
						<para>Skip the playback of the channel type (i.e. SIP, IAX, etc) when
						speaking the selected channel name.</para>
					</option>
					<option name="S">
						<para>Stop when no more channels are left to spy on.</para>
					</option>
					<option name="u">
						<para>The <literal>chanprefix</literal> parameter is a channel uniqueid
						or fully specified channel name.</para>
					</option>
					<option name="v">
						<argument name="value" />
						<para>Adjust the initial volume in the range from <literal>-4</literal>
						to <literal>4</literal>. A negative value refers to a quieter setting.</para>
					</option>
					<option name="w">
						<para>Enable <literal>whisper</literal> mode, so the spying channel can talk to
						the spied-on channel.</para>
					</option>
					<option name="W">
						<para>Enable <literal>private whisper</literal> mode, so the spying channel can
						talk to the spied-on channel but cannot listen to that channel.</para>
					</option>
					<option name="x">
						<argument name="digit" required="true">
							<para>Specify a DTMF digit that can be used to exit the application while actively
							spying on a channel. If there is no channel being spied on, the DTMF digit will be
							ignored.</para>
						</argument>
					</option>
					<option name="X">
						<para>Allow the user to exit ChanSpy to a valid single digit
						numeric extension in the current context or the context
						specified by the <variable>SPY_EXIT_CONTEXT</variable> channel variable. The
						name of the last channel that was spied on will be stored
						in the <variable>SPY_CHANNEL</variable> variable.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application is used to listen to the audio from an Asterisk channel. This includes the audio
			coming in and out of the channel being spied on. If the <literal>chanprefix</literal> parameter is specified,
			only channels beginning with this string will be spied upon.</para>
			<para>While spying, the following actions may be performed:</para>
			<para> - Dialing <literal>#</literal> cycles the volume level.</para>
			<para> - Dialing <literal>*</literal> will stop spying and look for another channel to spy on.</para>
			<para> - Dialing a series of digits followed by <literal>#</literal> builds a channel name to append
			to <literal>chanprefix</literal>. For example, executing ChanSpy(Agent) and then dialing the digits '1234#'
			while spying will begin spying on the channel 'Agent/1234'. Note that this feature will be overridden
			if the 'd' or 'u' options are used.</para>
			<note><para>The <replaceable>X</replaceable> option supersedes the three features above in that if a valid
			single digit extension exists in the correct context ChanSpy will exit to it.
			This also disables choosing a channel based on <literal>chanprefix</literal> and a digit sequence.</para></note>
		</description>
		<see-also>
			<ref type="application">ExtenSpy</ref>
			<ref type="managerEvent">ChanSpyStart</ref>
			<ref type="managerEvent">ChanSpyStop</ref>
		</see-also>
	</application>
	<application name="ExtenSpy" language="en_US">
		<synopsis>
			Listen to a channel, and optionally whisper into it.
		</synopsis>
		<syntax>
			<parameter name="exten" required="true" argsep="@">
				<argument name="exten" required="true">
					<para>Specify extension.</para>
				</argument>
				<argument name="context">
					<para>Optionally specify a context, defaults to <literal>default</literal>.</para>
				</argument>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="b">
						<para>Only spy on channels involved in a bridged call.</para>
					</option>
					<option name="B">
						<para>Instead of whispering on a single channel barge in on both
						channels involved in the call.</para>
					</option>
					<option name="c">
						<argument name="digit" required="true">
							<para>Specify a DTMF digit that can be used to spy on the next available channel.</para>
						</argument>
					</option>
					<option name="d">
						<para>Override the typical numeric DTMF functionality and instead
						use DTMF to switch between spy modes.</para>
						<enumlist>
							<enum name="4">
								<para>spy mode</para>
							</enum>
							<enum name="5">
								<para>whisper mode</para>
							</enum>
							<enum name="6">
								<para>barge mode</para>
							</enum>
						</enumlist>
					</option>
					<option name="e">
						<argument name="ext" required="true" />
						<para>Enable <emphasis>enforced</emphasis> mode, so the spying channel can
						only monitor extensions whose name is in the <replaceable>ext</replaceable> : delimited
						list.</para>
					</option>
					<option name="E">
						<para>Exit when the spied-on channel hangs up.</para>
					</option>
					<option name="g">
						<argument name="grp" required="true">
							<para>Only spy on channels in which one or more of the groups
							listed in <replaceable>grp</replaceable> matches one or more groups from the
							<variable>SPYGROUP</variable> variable set on the channel to be spied upon.</para>
						</argument>
						<note><para>both <replaceable>grp</replaceable> and <variable>SPYGROUP</variable> can contain
						either a single group or a colon-delimited list of groups, such
						as <literal>sales:support:accounting</literal>.</para></note>
					</option>
					<option name="n" argsep="@">
						<para>Say the name of the person being spied on if that person has recorded
						his/her name. If a context is specified, then that voicemail context will
						be searched when retrieving the name, otherwise the <literal>default</literal> context
						be used when searching for the name (i.e. if SIP/1000 is the channel being
						spied on and no mailbox is specified, then <literal>1000</literal> will be used when searching
						for the name).</para>
						<argument name="mailbox" />
						<argument name="context" />
					</option>
					<option name="o">
						<para>Only listen to audio coming from this channel.</para>
					</option>
					<option name="q">
						<para>Don't play a beep when beginning to spy on a channel, or speak the
						selected channel name.</para>
					</option>
					<option name="r">
						<para>Record the session to the monitor spool directory. An optional base for the filename
						may be specified. The default is <literal>chanspy</literal>.</para>
						<argument name="basename" />
					</option>
					<option name="s">
						<para>Skip the playback of the channel type (i.e. SIP, IAX, etc) when
						speaking the selected channel name.</para>
					</option>
					<option name="S">
						<para>Stop when there are no more extensions left to spy on.</para>
					</option>
					<option name="v">
						<argument name="value" />
						<para>Adjust the initial volume in the range from <literal>-4</literal>
						to <literal>4</literal>. A negative value refers to a quieter setting.</para>
					</option>
					<option name="w">
						<para>Enable <literal>whisper</literal> mode, so the spying channel can talk to
						the spied-on channel.</para>
					</option>
					<option name="W">
						<para>Enable <literal>private whisper</literal> mode, so the spying channel can
						talk to the spied-on channel but cannot listen to that channel.</para>
					</option>
					<option name="x">
						<argument name="digit" required="true">
							<para>Specify a DTMF digit that can be used to exit the application while actively
							spying on a channel. If there is no channel being spied on, the DTMF digit will be
							ignored.</para>
						</argument>
					</option>
					<option name="X">
						<para>Allow the user to exit ChanSpy to a valid single digit
						numeric extension in the current context or the context
						specified by the <variable>SPY_EXIT_CONTEXT</variable> channel variable. The
						name of the last channel that was spied on will be stored
						in the <variable>SPY_CHANNEL</variable> variable.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application is used to listen to the audio from an Asterisk channel. This includes
			the audio coming in and out of the channel being spied on. Only channels created by outgoing calls for the
			specified extension will be selected for spying. If the optional context is not supplied,
			the current channel's context will be used.</para>
			<para>While spying, the following actions may be performed:</para>
			<para> - Dialing <literal>#</literal> cycles the volume level.</para>
                        <para> - Dialing <literal>*</literal> will stop spying and look for another channel to spy on.</para>
			<note><para>The <replaceable>X</replaceable> option supersedes the three features above in that if a valid
			single digit extension exists in the correct context ChanSpy will exit to it.
			This also disables choosing a channel based on <literal>chanprefix</literal> and a digit sequence.</para></note>
		</description>
		<see-also>
			<ref type="application">ChanSpy</ref>
			<ref type="managerEvent">ChanSpyStart</ref>
			<ref type="managerEvent">ChanSpyStop</ref>
		</see-also>
	</application>
	<application name="DAHDIScan" language="en_US">
		<synopsis>
			Scan DAHDI channels to monitor calls.
		</synopsis>
		<syntax>
			<parameter name="group">
				<para>Limit scanning to a channel <replaceable>group</replaceable> by setting this option.</para>
			</parameter>
		</syntax>
		<description>
			<para>Allows a call center manager to monitor DAHDI channels in a
			convenient way.  Use <literal>#</literal> to select the next channel and use <literal>*</literal> to exit.</para>
		</description>
		<see-also>
			<ref type="managerEvent">ChanSpyStart</ref>
			<ref type="managerEvent">ChanSpyStop</ref>
		</see-also>
	</application>
 ***/

static const char app_chan[] = "ChanSpy";

static const char app_ext[] = "ExtenSpy";

static const char app_dahdiscan[] = "DAHDIScan";

enum {
	OPTION_QUIET             = (1 << 0),    /* Quiet, no announcement */
	OPTION_BRIDGED           = (1 << 1),    /* Only look at bridged calls */
	OPTION_VOLUME            = (1 << 2),    /* Specify initial volume */
	OPTION_GROUP             = (1 << 3),    /* Only look at channels in group */
	OPTION_RECORD            = (1 << 4),
	OPTION_WHISPER           = (1 << 5),
	OPTION_PRIVATE           = (1 << 6),    /* Private Whisper mode */
	OPTION_READONLY          = (1 << 7),    /* Don't mix the two channels */
	OPTION_EXIT              = (1 << 8),    /* Exit to a valid single digit extension */
	OPTION_ENFORCED          = (1 << 9),    /* Enforced mode */
	OPTION_NOTECH            = (1 << 10),   /* Skip technology name playback */
	OPTION_BARGE             = (1 << 11),   /* Barge mode (whisper to both channels) */
	OPTION_NAME              = (1 << 12),   /* Say the name of the person on whom we will spy */
	OPTION_DTMF_SWITCH_MODES = (1 << 13),   /* Allow numeric DTMF to switch between chanspy modes */
	OPTION_DTMF_EXIT         = (1 << 14),	/* Set DTMF to exit, added for DAHDIScan integration */
	OPTION_DTMF_CYCLE        = (1 << 15),	/* Custom DTMF for cycling next available channel, (default is '*') */
	OPTION_DAHDI_SCAN        = (1 << 16),	/* Scan groups in DAHDIScan mode */
	OPTION_STOP              = (1 << 17),
	OPTION_EXITONHANGUP      = (1 << 18),   /* Hang up when the spied-on channel hangs up. */
	OPTION_UNIQUEID          = (1 << 19),	/* The chanprefix is a channel uniqueid or fully specified channel name. */
};

enum {
	OPT_ARG_VOLUME = 0,
	OPT_ARG_GROUP,
	OPT_ARG_RECORD,
	OPT_ARG_ENFORCED,
	OPT_ARG_NAME,
	OPT_ARG_EXIT,
	OPT_ARG_CYCLE,
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(spy_opts, {
	AST_APP_OPTION('b', OPTION_BRIDGED),
	AST_APP_OPTION('B', OPTION_BARGE),
	AST_APP_OPTION_ARG('c', OPTION_DTMF_CYCLE, OPT_ARG_CYCLE),
	AST_APP_OPTION('d', OPTION_DTMF_SWITCH_MODES),
	AST_APP_OPTION_ARG('e', OPTION_ENFORCED, OPT_ARG_ENFORCED),
	AST_APP_OPTION('E', OPTION_EXITONHANGUP),
	AST_APP_OPTION_ARG('g', OPTION_GROUP, OPT_ARG_GROUP),
	AST_APP_OPTION_ARG('n', OPTION_NAME, OPT_ARG_NAME),
	AST_APP_OPTION('o', OPTION_READONLY),
	AST_APP_OPTION('q', OPTION_QUIET),
	AST_APP_OPTION_ARG('r', OPTION_RECORD, OPT_ARG_RECORD),
	AST_APP_OPTION('s', OPTION_NOTECH),
	AST_APP_OPTION('S', OPTION_STOP),
	AST_APP_OPTION('u', OPTION_UNIQUEID),
	AST_APP_OPTION_ARG('v', OPTION_VOLUME, OPT_ARG_VOLUME),
	AST_APP_OPTION('w', OPTION_WHISPER),
	AST_APP_OPTION('W', OPTION_PRIVATE),
	AST_APP_OPTION_ARG('x', OPTION_DTMF_EXIT, OPT_ARG_EXIT),
	AST_APP_OPTION('X', OPTION_EXIT),
});

struct chanspy_translation_helper {
	/* spy data */
	struct ast_audiohook spy_audiohook;
	struct ast_audiohook whisper_audiohook;
	struct ast_audiohook bridge_whisper_audiohook;
	int fd;
	int volfactor;
	struct ast_flags flags;
};

struct spy_dtmf_options {
	char exit;
	char cycle;
	char volume;
};

static void *spy_alloc(struct ast_channel *chan, void *data)
{
	/* just store the data pointer in the channel structure */
	return data;
}

static void spy_release(struct ast_channel *chan, void *data)
{
	/* nothing to do */
}

static int spy_generate(struct ast_channel *chan, void *data, int len, int samples)
{
	struct chanspy_translation_helper *csth = data;
	struct ast_frame *f, *cur;

	ast_audiohook_lock(&csth->spy_audiohook);
	if (csth->spy_audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING) {
		/* Channel is already gone more than likely */
		ast_audiohook_unlock(&csth->spy_audiohook);
		return -1;
	}

	if (ast_test_flag(&csth->flags, OPTION_READONLY)) {
		/* Option 'o' was set, so don't mix channel audio */
		f = ast_audiohook_read_frame(&csth->spy_audiohook, samples, AST_AUDIOHOOK_DIRECTION_READ, ast_format_slin);
	} else {
		f = ast_audiohook_read_frame(&csth->spy_audiohook, samples, AST_AUDIOHOOK_DIRECTION_BOTH, ast_format_slin);
	}

	ast_audiohook_unlock(&csth->spy_audiohook);

	if (!f)
		return 0;

	for (cur = f; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
		if (ast_write(chan, cur)) {
			ast_frfree(f);
			return -1;
		}

		if (csth->fd) {
			if (write(csth->fd, cur->data.ptr, cur->datalen) < 0) {
				ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
			}
		}
	}

	ast_frfree(f);

	return 0;
}

static struct ast_generator spygen = {
	.alloc = spy_alloc,
	.release = spy_release,
	.generate = spy_generate,
};

static int start_spying(struct ast_autochan *autochan, const char *spychan_name, struct ast_audiohook *audiohook)
{
	ast_log(LOG_NOTICE, "Attaching %s to %s\n", spychan_name, ast_channel_name(autochan->chan));

	ast_set_flag(audiohook, AST_AUDIOHOOK_TRIGGER_SYNC | AST_AUDIOHOOK_SMALL_QUEUE);
	return ast_audiohook_attach(autochan->chan, audiohook);
}

static void change_spy_mode(const char digit, struct ast_flags *flags)
{
	if (digit == '4') {
		ast_clear_flag(flags, OPTION_WHISPER);
		ast_clear_flag(flags, OPTION_BARGE);
	} else if (digit == '5') {
		ast_clear_flag(flags, OPTION_BARGE);
		ast_set_flag(flags, OPTION_WHISPER);
	} else if (digit == '6') {
		ast_clear_flag(flags, OPTION_WHISPER);
		ast_set_flag(flags, OPTION_BARGE);
	}
}

static int pack_channel_into_message(struct ast_channel *chan, const char *role,
									 struct ast_multi_channel_blob *payload)
{
	RAII_VAR(struct ast_channel_snapshot *, snapshot,
			ast_channel_snapshot_get_latest(ast_channel_uniqueid(chan)),
			ao2_cleanup);

	if (!snapshot) {
		return -1;
	}
	ast_multi_channel_blob_add_channel(payload, role, snapshot);
	return 0;
}

/*! \internal
 * \brief Publish the chanspy message over Stasis-Core
 * \param spyer The channel doing the spying
 * \param spyee Who is being spied upon
 * \start start If non-zero, the spying is starting. Otherwise, the spyer is
 * finishing
 */
static void publish_chanspy_message(struct ast_channel *spyer,
									struct ast_channel *spyee,
									int start)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
	RAII_VAR(struct ast_multi_channel_blob *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	struct stasis_message_type *type = start ? ast_channel_chanspy_start_type(): ast_channel_chanspy_stop_type();

	if (!spyer) {
		ast_log(AST_LOG_WARNING, "Attempt to publish ChanSpy message for NULL spyer channel\n");
		return;
	}
	blob = ast_json_null();
	if (!blob || !type) {
		return;
	}

	payload = ast_multi_channel_blob_create(blob);
	if (!payload) {
		return;
	}

	if (pack_channel_into_message(spyer, "spyer_channel", payload)) {
		return;
	}

	if (spyee) {
		if (pack_channel_into_message(spyee, "spyee_channel", payload)) {
			return;
		}
	}

	message = stasis_message_create(type, payload);
	if (!message) {
		return;
	}
	stasis_publish(ast_channel_topic(spyer), message);
}

static int attach_barge(struct ast_autochan *spyee_autochan,
	struct ast_autochan **spyee_bridge_autochan, struct ast_audiohook *bridge_whisper_audiohook,
	const char *spyer_name, const char *name)
{
	int retval = 0;
	struct ast_autochan *internal_bridge_autochan;
	RAII_VAR(struct ast_channel *, bridged, ast_channel_bridge_peer(spyee_autochan->chan), ast_channel_cleanup);

	if (!bridged) {
		return -1;
	}

	ast_audiohook_init(bridge_whisper_audiohook, AST_AUDIOHOOK_TYPE_WHISPER, "Chanspy", 0);

	internal_bridge_autochan = ast_autochan_setup(bridged);
	if (!internal_bridge_autochan) {
		return -1;
	}

	ast_channel_lock(internal_bridge_autochan->chan);
	if (start_spying(internal_bridge_autochan, spyer_name, bridge_whisper_audiohook)) {
		ast_log(LOG_WARNING, "Unable to attach barge audiohook on spyee '%s'. Barge mode disabled.\n", name);
		retval = -1;
	}
	ast_channel_unlock(internal_bridge_autochan->chan);

	*spyee_bridge_autochan = internal_bridge_autochan;

	return retval;
}

static int channel_spy(struct ast_channel *chan, struct ast_autochan *spyee_autochan,
	int *volfactor, int fd, struct spy_dtmf_options *user_options, struct ast_flags *flags,
	char *exitcontext)
{
	struct chanspy_translation_helper csth;
	int running = 0, bridge_connected = 0, res, x = 0;
	char inp[24] = {0};
	char *name;
	struct ast_frame *f;
	struct ast_silence_generator *silgen = NULL;
	struct ast_autochan *spyee_bridge_autochan = NULL;
	const char *spyer_name;

	if (ast_check_hangup(chan) || ast_check_hangup(spyee_autochan->chan) ||
			ast_test_flag(ast_channel_flags(spyee_autochan->chan), AST_FLAG_ZOMBIE)) {
		return 0;
	}

	ast_channel_lock(chan);
	spyer_name = ast_strdupa(ast_channel_name(chan));
	ast_channel_unlock(chan);

	ast_channel_lock(spyee_autochan->chan);
	name = ast_strdupa(ast_channel_name(spyee_autochan->chan));
	ast_channel_unlock(spyee_autochan->chan);

	ast_verb(2, "Spying on channel %s\n", name);
	publish_chanspy_message(chan, spyee_autochan->chan, 1);

	memset(&csth, 0, sizeof(csth));
	ast_copy_flags(&csth.flags, flags, AST_FLAGS_ALL);

	/* This is the audiohook which gives us the audio off the channel we are
	   spying on.
	*/
	ast_audiohook_init(&csth.spy_audiohook, AST_AUDIOHOOK_TYPE_SPY, "ChanSpy", 0);

	if (start_spying(spyee_autochan, spyer_name, &csth.spy_audiohook)) {
		ast_audiohook_destroy(&csth.spy_audiohook);
		return 0;
	}

	if (ast_test_flag(flags, OPTION_WHISPER | OPTION_BARGE | OPTION_DTMF_SWITCH_MODES)) {
		/* This audiohook will let us inject audio from our channel into the
		   channel we are currently spying on.
		*/
		ast_audiohook_init(&csth.whisper_audiohook, AST_AUDIOHOOK_TYPE_WHISPER, "ChanSpy", 0);

		if (start_spying(spyee_autochan, spyer_name, &csth.whisper_audiohook)) {
			ast_log(LOG_WARNING, "Unable to attach whisper audiohook to spyee %s. Whisper mode disabled!\n", name);
		}
	}

	ast_channel_lock(chan);
	ast_set_flag(ast_channel_flags(chan), AST_FLAG_END_DTMF_ONLY);
	ast_channel_unlock(chan);

	csth.volfactor = *volfactor;

	if (csth.volfactor) {
		csth.spy_audiohook.options.read_volume = csth.volfactor;
		csth.spy_audiohook.options.write_volume = csth.volfactor;
	}

	csth.fd = fd;

	if (ast_test_flag(flags, OPTION_PRIVATE))
		silgen = ast_channel_start_silence_generator(chan);
	else
		ast_activate_generator(chan, &spygen, &csth);

	/* We can no longer rely on 'spyee' being an actual channel;
	   it can be hung up and freed out from under us. However, the
	   channel destructor will put NULL into our csth.spy.chan
	   field when that happens, so that is our signal that the spyee
	   channel has gone away.
	*/

	/* Note: it is very important that the ast_waitfor() be the first
	   condition in this expression, so that if we wait for some period
	   of time before receiving a frame from our spying channel, we check
	   for hangup on the spied-on channel _after_ knowing that a frame
	   has arrived, since the spied-on channel could have gone away while
	   we were waiting
	*/
	while (ast_waitfor(chan, -1) > -1 && csth.spy_audiohook.status == AST_AUDIOHOOK_STATUS_RUNNING) {
		if (!(f = ast_read(chan)) || ast_check_hangup(chan)) {
			running = -1;
			if (f) {
				ast_frfree(f);
			}
			break;
		}

		if (ast_test_flag(flags, OPTION_BARGE) && f->frametype == AST_FRAME_VOICE) {
			/* This hook lets us inject audio into the channel that the spyee is currently
			 * bridged with. If the spyee isn't bridged with anything yet, nothing will
			 * be attached and we'll need to continue attempting to attach the barge
			 * audio hook. */
			if (!bridge_connected && attach_barge(spyee_autochan, &spyee_bridge_autochan,
					&csth.bridge_whisper_audiohook, spyer_name, name) == 0) {
				bridge_connected = 1;
			}

			ast_audiohook_lock(&csth.whisper_audiohook);
			ast_audiohook_write_frame(&csth.whisper_audiohook, AST_AUDIOHOOK_DIRECTION_WRITE, f);
			ast_audiohook_unlock(&csth.whisper_audiohook);

			if (bridge_connected) {
				ast_audiohook_lock(&csth.bridge_whisper_audiohook);
				ast_audiohook_write_frame(&csth.bridge_whisper_audiohook, AST_AUDIOHOOK_DIRECTION_WRITE, f);
				ast_audiohook_unlock(&csth.bridge_whisper_audiohook);
			}

			ast_frfree(f);
			continue;
		} else if (ast_test_flag(flags, OPTION_WHISPER) && f->frametype == AST_FRAME_VOICE) {
			ast_audiohook_lock(&csth.whisper_audiohook);
			ast_audiohook_write_frame(&csth.whisper_audiohook, AST_AUDIOHOOK_DIRECTION_WRITE, f);
			ast_audiohook_unlock(&csth.whisper_audiohook);
			ast_frfree(f);
			continue;
		}

		res = (f->frametype == AST_FRAME_DTMF) ? f->subclass.integer : 0;
		ast_frfree(f);
		if (!res)
			continue;

		if (x == sizeof(inp))
			x = 0;

		if (res < 0) {
			running = -1;
			break;
		}

		if (ast_test_flag(flags, OPTION_EXIT)) {
			char tmp[2];
			tmp[0] = res;
			tmp[1] = '\0';
			if (!ast_goto_if_exists(chan, exitcontext, tmp, 1)) {
				ast_debug(1, "Got DTMF %c, goto context %s\n", tmp[0], exitcontext);
				pbx_builtin_setvar_helper(chan, "SPY_CHANNEL", name);
				running = -2;
				break;
			} else {
				ast_debug(2, "Exit by single digit did not work in chanspy. Extension %s does not exist in context %s\n", tmp, exitcontext);
			}
		} else if (res >= '0' && res <= '9') {
			if (ast_test_flag(flags, OPTION_DTMF_SWITCH_MODES)) {
				change_spy_mode(res, flags);
			} else {
				inp[x++] = res;
			}
		}

		if (res == user_options->cycle) {
			running = 0;
			break;
		} else if (res == user_options->exit) {
			running = -2;
			break;
		} else if (res == user_options->volume) {
			if (!ast_strlen_zero(inp)) {
				running = atoi(inp);
				break;
			}

			(*volfactor)++;
			if (*volfactor > 4)
				*volfactor = -4;
			ast_verb(3, "Setting spy volume on %s to %d\n", ast_channel_name(chan), *volfactor);

			csth.volfactor = *volfactor;
			csth.spy_audiohook.options.read_volume = csth.volfactor;
			csth.spy_audiohook.options.write_volume = csth.volfactor;
		}
	}

	if (ast_test_flag(flags, OPTION_PRIVATE))
		ast_channel_stop_silence_generator(chan, silgen);
	else
		ast_deactivate_generator(chan);

	ast_channel_lock(chan);
	ast_clear_flag(ast_channel_flags(chan), AST_FLAG_END_DTMF_ONLY);
	ast_channel_unlock(chan);

	if (ast_test_flag(flags, OPTION_WHISPER | OPTION_BARGE | OPTION_DTMF_SWITCH_MODES)) {
		ast_audiohook_lock(&csth.whisper_audiohook);
		ast_audiohook_detach(&csth.whisper_audiohook);
		ast_audiohook_unlock(&csth.whisper_audiohook);
		ast_audiohook_destroy(&csth.whisper_audiohook);
	}

	if (ast_test_flag(flags, OPTION_BARGE | OPTION_DTMF_SWITCH_MODES)) {
		ast_audiohook_lock(&csth.bridge_whisper_audiohook);
		ast_audiohook_detach(&csth.bridge_whisper_audiohook);
		ast_audiohook_unlock(&csth.bridge_whisper_audiohook);
		ast_audiohook_destroy(&csth.bridge_whisper_audiohook);
	}

	ast_audiohook_lock(&csth.spy_audiohook);
	ast_audiohook_detach(&csth.spy_audiohook);
	ast_audiohook_unlock(&csth.spy_audiohook);
	ast_audiohook_destroy(&csth.spy_audiohook);

	if (spyee_bridge_autochan) {
		ast_autochan_destroy(spyee_bridge_autochan);
	}

	ast_verb(2, "Done Spying on channel %s\n", name);
	publish_chanspy_message(chan, NULL, 0);

	return running;
}

static struct ast_autochan *next_channel(struct ast_channel_iterator *iter,
		struct ast_autochan *autochan, struct ast_channel *chan)
{
	struct ast_channel *next;
	struct ast_autochan *autochan_store;
	const size_t pseudo_len = strlen("DAHDI/pseudo");

	if (!iter) {
		return NULL;
	}

	for (; (next = ast_channel_iterator_next(iter)); ast_channel_unref(next)) {
		if (!strncmp(ast_channel_name(next), "DAHDI/pseudo", pseudo_len)
			|| next == chan) {
			continue;
		}

		autochan_store = ast_autochan_setup(next);
		ast_channel_unref(next);

		return autochan_store;
	}
	return NULL;
}

static int spy_sayname(struct ast_channel *chan, const char *mailbox, const char *context)
{
	char *mailbox_id;

	mailbox_id = ast_alloca(strlen(mailbox) + strlen(context) + 2);
	sprintf(mailbox_id, "%s@%s", mailbox, context); /* Safe */
	return ast_app_sayname(chan, mailbox_id);
}

static int common_exec(struct ast_channel *chan, struct ast_flags *flags,
	int volfactor, const int fd, struct spy_dtmf_options *user_options,
	const char *mygroup, const char *myenforced, const char *spec, const char *exten,
	const char *context, const char *mailbox, const char *name_context)
{
	char nameprefix[AST_NAME_STRLEN];
	char exitcontext[AST_MAX_CONTEXT] = "";
	signed char zero_volume = 0;
	int waitms;
	int res;
	int num_spyed_upon = 1;
	struct ast_channel_iterator *iter = NULL;

	if (ast_test_flag(flags, OPTION_EXIT)) {
		const char *c;
		ast_channel_lock(chan);
		if ((c = pbx_builtin_getvar_helper(chan, "SPY_EXIT_CONTEXT"))) {
			ast_copy_string(exitcontext, c, sizeof(exitcontext));
		} else if (!ast_strlen_zero(ast_channel_macrocontext(chan))) {
			ast_copy_string(exitcontext, ast_channel_macrocontext(chan), sizeof(exitcontext));
		} else {
			ast_copy_string(exitcontext, ast_channel_context(chan), sizeof(exitcontext));
		}
		ast_channel_unlock(chan);
	}

	if (ast_channel_state(chan) != AST_STATE_UP)
		ast_answer(chan);

	ast_set_flag(ast_channel_flags(chan), AST_FLAG_SPYING); /* so nobody can spy on us while we are spying */

	waitms = 100;

	for (;;) {
		struct ast_autochan *autochan = NULL, *next_autochan = NULL;
		struct ast_channel *prev = NULL;

		if (!ast_test_flag(flags, OPTION_QUIET) && num_spyed_upon) {
			res = ast_streamfile(chan, "beep", ast_channel_language(chan));
			if (!res)
				res = ast_waitstream(chan, "");
			else if (res < 0) {
				ast_clear_flag(ast_channel_flags(chan), AST_FLAG_SPYING);
				break;
			}
			if (!ast_strlen_zero(exitcontext)) {
				char tmp[2];
				tmp[0] = res;
				tmp[1] = '\0';
				if (!ast_goto_if_exists(chan, exitcontext, tmp, 1))
					goto exit;
				else
					ast_debug(2, "Exit by single digit did not work in chanspy. Extension %s does not exist in context %s\n", tmp, exitcontext);
			}
		}

		/* Set up the iterator we'll be using during this call */
		if (!ast_strlen_zero(spec)) {
			if (ast_test_flag(flags, OPTION_UNIQUEID)) {
				struct ast_channel *unique_chan;

				unique_chan = ast_channel_get_by_name(spec);
				if (!unique_chan) {
					res = -1;
					goto exit;
				}
				iter = ast_channel_iterator_by_name_new(ast_channel_name(unique_chan), 0);
				ast_channel_unref(unique_chan);
			} else {
				iter = ast_channel_iterator_by_name_new(spec, strlen(spec));
			}
		} else if (!ast_strlen_zero(exten)) {
			iter = ast_channel_iterator_by_exten_new(exten, context);
		} else {
			iter = ast_channel_iterator_all_new();
		}

		if (!iter) {
			res = -1;
			goto exit;
		}

		res = ast_waitfordigit(chan, waitms);
		if (res < 0) {
			iter = ast_channel_iterator_destroy(iter);
			ast_clear_flag(ast_channel_flags(chan), AST_FLAG_SPYING);
			break;
		}
		if (!ast_strlen_zero(exitcontext)) {
			char tmp[2];
			tmp[0] = res;
			tmp[1] = '\0';
			if (!ast_goto_if_exists(chan, exitcontext, tmp, 1)) {
				iter = ast_channel_iterator_destroy(iter);
				goto exit;
			} else {
				ast_debug(2, "Exit by single digit did not work in chanspy. Extension %s does not exist in context %s\n", tmp, exitcontext);
			}
		}

		/* reset for the next loop around, unless overridden later */
		waitms = 100;
		num_spyed_upon = 0;

		for (autochan = next_channel(iter, autochan, chan);
		     autochan;
			 prev = autochan->chan, ast_autochan_destroy(autochan),
		     autochan = next_autochan ? next_autochan :
				next_channel(iter, autochan, chan), next_autochan = NULL) {
			int igrp = !mygroup;
			int ienf = !myenforced;

			if (autochan->chan == prev) {
				ast_autochan_destroy(autochan);
				break;
			}

			if (ast_check_hangup(chan)) {
				ast_autochan_destroy(autochan);
				break;
			}

			if (ast_test_flag(flags, OPTION_BRIDGED) && !ast_channel_is_bridged(autochan->chan)) {
				continue;
			}

			if (ast_check_hangup(autochan->chan) || ast_test_flag(ast_channel_flags(autochan->chan), AST_FLAG_SPYING)) {
				continue;
			}

			if (mygroup) {
				int num_groups = 0;
				int num_mygroups = 0;
				char dup_group[512];
				char dup_mygroup[512];
				char *groups[NUM_SPYGROUPS];
				char *mygroups[NUM_SPYGROUPS];
				const char *group = NULL;
				int x;
				int y;
				ast_copy_string(dup_mygroup, mygroup, sizeof(dup_mygroup));
				num_mygroups = ast_app_separate_args(dup_mygroup, ':', mygroups,
					ARRAY_LEN(mygroups));

				/* Before dahdi scan was part of chanspy, it would use the "GROUP" variable
				 * rather than "SPYGROUP", this check is done to preserve expected behavior */
				if (ast_test_flag(flags, OPTION_DAHDI_SCAN)) {
					group = pbx_builtin_getvar_helper(autochan->chan, "GROUP");
				} else {
					group = pbx_builtin_getvar_helper(autochan->chan, "SPYGROUP");
				}

				if (!ast_strlen_zero(group)) {
					ast_copy_string(dup_group, group, sizeof(dup_group));
					num_groups = ast_app_separate_args(dup_group, ':', groups,
						ARRAY_LEN(groups));
				}

				for (y = 0; y < num_mygroups; y++) {
					for (x = 0; x < num_groups; x++) {
						if (!strcmp(mygroups[y], groups[x])) {
							igrp = 1;
							break;
						}
					}
				}
			}

			if (!igrp) {
				continue;
			}
			if (myenforced) {
				char ext[AST_CHANNEL_NAME + 3];
				char buffer[512];
				char *end;

				snprintf(buffer, sizeof(buffer) - 1, ":%s:", myenforced);

				ast_copy_string(ext + 1, ast_channel_name(autochan->chan), sizeof(ext) - 1);
				if ((end = strchr(ext, '-'))) {
					*end++ = ':';
					*end = '\0';
				}

				ext[0] = ':';

				if (strcasestr(buffer, ext)) {
					ienf = 1;
				}
			}

			if (!ienf) {
				continue;
			}

			if (!ast_test_flag(flags, OPTION_QUIET)) {
				char peer_name[AST_NAME_STRLEN + 5];
				char *ptr, *s;

				strcpy(peer_name, "spy-");
				strncat(peer_name, ast_channel_name(autochan->chan), AST_NAME_STRLEN - 4 - 1);
				if ((ptr = strchr(peer_name, '/'))) {
					*ptr++ = '\0';
					for (s = peer_name; s < ptr; s++) {
						*s = tolower(*s);
					}
					if ((s = strchr(ptr, '-'))) {
						*s = '\0';
					}
				}

				if (ast_test_flag(flags, OPTION_NAME)) {
					const char *local_context = S_OR(name_context, "default");
					const char *local_mailbox = S_OR(mailbox, ptr);

					if (local_mailbox) {
						res = spy_sayname(chan, local_mailbox, local_context);
					} else {
						res = -1;
					}
				}
				if (!ast_test_flag(flags, OPTION_NAME) || res < 0) {
					int num;
					if (!ast_test_flag(flags, OPTION_NOTECH)) {
						if (ast_fileexists(peer_name, NULL, NULL) > 0) {
							res = ast_streamfile(chan, peer_name, ast_channel_language(chan));
							if (!res) {
								res = ast_waitstream(chan, "");
							}
							if (res) {
								ast_autochan_destroy(autochan);
								break;
							}
						} else {
							res = ast_say_character_str(chan, peer_name, "", ast_channel_language(chan), AST_SAY_CASE_NONE);
						}
					}
					if (ptr && (num = atoi(ptr))) {
						ast_say_digits(chan, num, "", ast_channel_language(chan));
					}
				}
			}

			res = channel_spy(chan, autochan, &volfactor, fd, user_options, flags, exitcontext);
			num_spyed_upon++;

			if (res == -1) {
				ast_autochan_destroy(autochan);
				iter = ast_channel_iterator_destroy(iter);
				goto exit;
			} else if (res == -2) {
				res = 0;
				ast_autochan_destroy(autochan);
				iter = ast_channel_iterator_destroy(iter);
				goto exit;
			} else if (res > 1 && spec && !ast_test_flag(flags, OPTION_UNIQUEID)) {
				struct ast_channel *next;

				snprintf(nameprefix, AST_NAME_STRLEN, "%s/%d", spec, res);

				if ((next = ast_channel_get_by_name_prefix(nameprefix, strlen(nameprefix)))) {
					next_autochan = ast_autochan_setup(next);
					next = ast_channel_unref(next);
				} else {
					/* stay on this channel, if it is still valid */
					if (!ast_check_hangup(autochan->chan)) {
						next_autochan = ast_autochan_setup(autochan->chan);
					} else {
						/* the channel is gone */
						next_autochan = NULL;
					}
				}
			} else if (res == 0 && ast_test_flag(flags, OPTION_EXITONHANGUP)) {
				ast_autochan_destroy(autochan);
				iter = ast_channel_iterator_destroy(iter);
				goto exit;
			}
		}

		iter = ast_channel_iterator_destroy(iter);

		if (res == -1 || ast_check_hangup(chan))
			break;
		if (ast_test_flag(flags, OPTION_STOP) && !next_autochan) {
			break;
		}
	}
exit:

	ast_clear_flag(ast_channel_flags(chan), AST_FLAG_SPYING);

	ast_channel_setoption(chan, AST_OPTION_TXGAIN, &zero_volume, sizeof(zero_volume), 0);

	return res;
}

static int chanspy_exec(struct ast_channel *chan, const char *data)
{
	char *myenforced = NULL;
	char *mygroup = NULL;
	char *recbase = NULL;
	int fd = 0;
	struct ast_flags flags;
	struct spy_dtmf_options user_options = {
		.cycle = '*',
		.volume = '#',
		.exit = '\0',
	};
	RAII_VAR(struct ast_format *, oldwf, NULL, ao2_cleanup);
	int volfactor = 0;
	int res;
	char *mailbox = NULL;
	char *name_context = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(spec);
		AST_APP_ARG(options);
	);
	char *opts[OPT_ARG_ARRAY_SIZE];
	char *parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.spec && !strcmp(args.spec, "all"))
		args.spec = NULL;

	if (args.options) {
		char tmp;
		ast_app_parse_options(spy_opts, &flags, opts, args.options);
		if (ast_test_flag(&flags, OPTION_GROUP))
			mygroup = opts[OPT_ARG_GROUP];

		if (ast_test_flag(&flags, OPTION_RECORD) &&
			!(recbase = opts[OPT_ARG_RECORD]))
			recbase = "chanspy";

		if (ast_test_flag(&flags, OPTION_DTMF_EXIT) && opts[OPT_ARG_EXIT]) {
			tmp = opts[OPT_ARG_EXIT][0];
			if (strchr("0123456789*#", tmp) && tmp != '\0') {
				user_options.exit = tmp;
			} else {
				ast_log(LOG_NOTICE, "Argument for option 'x' must be a valid DTMF digit.\n");
			}
		}

		if (ast_test_flag(&flags, OPTION_DTMF_CYCLE) && opts[OPT_ARG_CYCLE]) {
			tmp = opts[OPT_ARG_CYCLE][0];
			if (strchr("0123456789*#", tmp) && tmp != '\0') {
				user_options.cycle = tmp;
			} else {
				ast_log(LOG_NOTICE, "Argument for option 'c' must be a valid DTMF digit.\n");
			}
		}

		if (ast_test_flag(&flags, OPTION_VOLUME) && opts[OPT_ARG_VOLUME]) {
			int vol;

			if ((sscanf(opts[OPT_ARG_VOLUME], "%30d", &vol) != 1) || (vol > 4) || (vol < -4))
				ast_log(LOG_NOTICE, "Volume factor must be a number between -4 and 4\n");
			else
				volfactor = vol;
		}

		if (ast_test_flag(&flags, OPTION_PRIVATE))
			ast_set_flag(&flags, OPTION_WHISPER);

		if (ast_test_flag(&flags, OPTION_ENFORCED))
			myenforced = opts[OPT_ARG_ENFORCED];

		if (ast_test_flag(&flags, OPTION_NAME)) {
			if (!ast_strlen_zero(opts[OPT_ARG_NAME])) {
				char *delimiter;
				if ((delimiter = strchr(opts[OPT_ARG_NAME], '@'))) {
					mailbox = opts[OPT_ARG_NAME];
					*delimiter++ = '\0';
					name_context = delimiter;
				} else {
					mailbox = opts[OPT_ARG_NAME];
				}
			}
		}
	} else {
		ast_clear_flag(&flags, AST_FLAGS_ALL);
	}

	oldwf = ao2_bump(ast_channel_writeformat(chan));
	if (ast_set_write_format(chan, ast_format_slin) < 0) {
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");
		return -1;
	}

	if (recbase) {
		char filename[PATH_MAX];

		snprintf(filename, sizeof(filename), "%s/%s.%d.raw", ast_config_AST_MONITOR_DIR, recbase, (int) time(NULL));
		if ((fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, AST_FILE_MODE)) <= 0) {
			ast_log(LOG_WARNING, "Cannot open '%s' for recording\n", filename);
			fd = 0;
		}
	}

	res = common_exec(chan, &flags, volfactor, fd, &user_options, mygroup, myenforced, args.spec, NULL, NULL, mailbox, name_context);

	if (fd)
		close(fd);

	if (oldwf && ast_set_write_format(chan, oldwf) < 0)
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");

	if (ast_test_flag(&flags, OPTION_EXITONHANGUP)) {
		ast_verb(3, "Stopped spying due to the spied-on channel hanging up.\n");
	}

	return res;
}

static int extenspy_exec(struct ast_channel *chan, const char *data)
{
	char *ptr, *exten = NULL;
	char *mygroup = NULL;
	char *recbase = NULL;
	int fd = 0;
	struct ast_flags flags;
	struct spy_dtmf_options user_options = {
		.cycle = '*',
		.volume = '#',
		.exit = '\0',
	};
	RAII_VAR(struct ast_format *, oldwf, NULL, ao2_cleanup);
	int volfactor = 0;
	int res;
	char *mailbox = NULL;
	char *name_context = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(context);
		AST_APP_ARG(options);
	);
	char *parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.context) && (ptr = strchr(args.context, '@'))) {
		exten = args.context;
		*ptr++ = '\0';
		args.context = ptr;
	}
	if (ast_strlen_zero(args.context))
		args.context = ast_strdupa(ast_channel_context(chan));

	if (args.options) {
		char *opts[OPT_ARG_ARRAY_SIZE];
		char tmp;

		ast_app_parse_options(spy_opts, &flags, opts, args.options);
		if (ast_test_flag(&flags, OPTION_GROUP))
			mygroup = opts[OPT_ARG_GROUP];

		if (ast_test_flag(&flags, OPTION_RECORD) &&
			!(recbase = opts[OPT_ARG_RECORD]))
			recbase = "chanspy";

		if (ast_test_flag(&flags, OPTION_DTMF_EXIT) && opts[OPT_ARG_EXIT]) {
			tmp = opts[OPT_ARG_EXIT][0];
			if (strchr("0123456789*#", tmp) && tmp != '\0') {
				user_options.exit = tmp;
			} else {
				ast_log(LOG_NOTICE, "Argument for option 'x' must be a valid DTMF digit.\n");
			}
		}

		if (ast_test_flag(&flags, OPTION_DTMF_CYCLE) && opts[OPT_ARG_CYCLE]) {
			tmp = opts[OPT_ARG_CYCLE][0];
			if (strchr("0123456789*#", tmp) && tmp != '\0') {
				user_options.cycle = tmp;
			} else {
				ast_log(LOG_NOTICE, "Argument for option 'c' must be a valid DTMF digit.\n");
			}
		}

		if (ast_test_flag(&flags, OPTION_VOLUME) && opts[OPT_ARG_VOLUME]) {
			int vol;

			if ((sscanf(opts[OPT_ARG_VOLUME], "%30d", &vol) != 1) || (vol > 4) || (vol < -4))
				ast_log(LOG_NOTICE, "Volume factor must be a number between -4 and 4\n");
			else
				volfactor = vol;
		}

		if (ast_test_flag(&flags, OPTION_PRIVATE))
			ast_set_flag(&flags, OPTION_WHISPER);

		if (ast_test_flag(&flags, OPTION_NAME)) {
			if (!ast_strlen_zero(opts[OPT_ARG_NAME])) {
				char *delimiter;
				if ((delimiter = strchr(opts[OPT_ARG_NAME], '@'))) {
					mailbox = opts[OPT_ARG_NAME];
					*delimiter++ = '\0';
					name_context = delimiter;
				} else {
					mailbox = opts[OPT_ARG_NAME];
				}
			}
		}

	} else {
		/* Coverity - This uninit_use should be ignored since this macro initializes the flags */
		ast_clear_flag(&flags, AST_FLAGS_ALL);
	}

	oldwf = ao2_bump(ast_channel_writeformat(chan));
	if (ast_set_write_format(chan, ast_format_slin) < 0) {
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");
		return -1;
	}

	if (recbase) {
		char filename[PATH_MAX];

		snprintf(filename, sizeof(filename), "%s/%s.%d.raw", ast_config_AST_MONITOR_DIR, recbase, (int) time(NULL));
		if ((fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, AST_FILE_MODE)) <= 0) {
			ast_log(LOG_WARNING, "Cannot open '%s' for recording\n", filename);
			fd = 0;
		}
	}


	res = common_exec(chan, &flags, volfactor, fd, &user_options, mygroup, NULL, NULL, exten, args.context, mailbox, name_context);

	if (fd)
		close(fd);

	if (oldwf && ast_set_write_format(chan, oldwf) < 0)
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");

	return res;
}

static int dahdiscan_exec(struct ast_channel *chan, const char *data)
{
	const char *spec = "DAHDI";
	struct ast_flags flags;
	struct spy_dtmf_options user_options = {
		.cycle = '#',
		.volume = '\0',
		.exit = '*',
	};
	struct ast_format *oldwf;
	int res;
	char *mygroup = NULL;

	/* Coverity - This uninit_use should be ignored since this macro initializes the flags */
	ast_clear_flag(&flags, AST_FLAGS_ALL);

	if (!ast_strlen_zero(data)) {
		mygroup = ast_strdupa(data);
	}
	ast_set_flag(&flags, OPTION_DTMF_EXIT);
	ast_set_flag(&flags, OPTION_DTMF_CYCLE);
	ast_set_flag(&flags, OPTION_DAHDI_SCAN);

	oldwf = ao2_bump(ast_channel_writeformat(chan));
	if (ast_set_write_format(chan, ast_format_slin) < 0) {
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");
		ao2_cleanup(oldwf);
		return -1;
	}

	res = common_exec(chan, &flags, 0, 0, &user_options, mygroup, NULL, spec, NULL, NULL, NULL, NULL);

	if (oldwf && ast_set_write_format(chan, oldwf) < 0)
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");
	ao2_cleanup(oldwf);

	return res;
}

static int unload_module(void)
{
	int res = 0;

	res |= ast_unregister_application(app_chan);
	res |= ast_unregister_application(app_ext);
	res |= ast_unregister_application(app_dahdiscan);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_register_application_xml(app_chan, chanspy_exec);
	res |= ast_register_application_xml(app_ext, extenspy_exec);
	res |= ast_register_application_xml(app_dahdiscan, dahdiscan_exec);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Listen to the audio of an active channel");
