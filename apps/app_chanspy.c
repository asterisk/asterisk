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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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
					<option name="o">
						<para>Only listen to audio coming from this channel.</para>
					</option>
					<option name="X">
						<para>Allow the user to exit ChanSpy to a valid single digit
						numeric extension in the current context or the context
						specified by the <variable>SPY_EXIT_CONTEXT</variable> channel variable. The
						name of the last channel that was spied on will be stored
						in the <variable>SPY_CHANNEL</variable> variable.</para>
					</option>
					<option name="e">
						<argument name="ext" required="true" />
						<para>Enable <emphasis>enforced</emphasis> mode, so the spying channel can
						only monitor extensions whose name is in the <replaceable>ext</replaceable> : delimited 
						list.</para>
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
			to 'chanprefix'. For example, executing ChanSpy(Agent) and then dialing the digits '1234#' 
			while spying will begin spying on the channel 'Agent/1234'. Note that this feature will be overridden if the 'd' option
			is used</para>
			<note><para>The <replaceable>X</replaceable> option supersedes the three features above in that if a valid
			single digit extension exists in the correct context ChanSpy will exit to it.
			This also disables choosing a channel based on <literal>chanprefix</literal> and a digit sequence.</para></note>
		</description>
		<see-also>
			<ref type="application">ExtenSpy</ref>
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
					<option name="o">
						<para>Only listen to audio coming from this channel.</para>
					</option>
					<option name="X">
						<para>Allow the user to exit ChanSpy to a valid single digit
						numeric extension in the current context or the context
						specified by the <variable>SPY_EXIT_CONTEXT</variable> channel variable. The
						name of the last channel that was spied on will be stored
						in the <variable>SPY_CHANNEL</variable> variable.</para>
					</option>
					<option name="e">
						<argument name="ext" required="true" />
						<para>Enable <emphasis>enforced</emphasis> mode, so the spying channel can
						only monitor extensions whose name is in the <replaceable>ext</replaceable> : delimited 
						list.</para>
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
		</see-also>
	</application>

 ***/
static const char *app_chan = "ChanSpy";

static const char *app_ext = "ExtenSpy";

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
	OPTION_DTMF_SWITCH_MODES = (1 << 13),   /*Allow numeric DTMF to switch between chanspy modes */
} chanspy_opt_flags;

enum {
	OPT_ARG_VOLUME = 0,
	OPT_ARG_GROUP,
	OPT_ARG_RECORD,
	OPT_ARG_ENFORCED,
	OPT_ARG_NAME,
	OPT_ARG_ARRAY_SIZE,
} chanspy_opt_args;

AST_APP_OPTIONS(spy_opts, {
	AST_APP_OPTION('q', OPTION_QUIET),
	AST_APP_OPTION('b', OPTION_BRIDGED),
	AST_APP_OPTION('B', OPTION_BARGE),
	AST_APP_OPTION('w', OPTION_WHISPER),
	AST_APP_OPTION('W', OPTION_PRIVATE),
	AST_APP_OPTION_ARG('v', OPTION_VOLUME, OPT_ARG_VOLUME),
	AST_APP_OPTION_ARG('g', OPTION_GROUP, OPT_ARG_GROUP),
	AST_APP_OPTION_ARG('r', OPTION_RECORD, OPT_ARG_RECORD),
	AST_APP_OPTION_ARG('e', OPTION_ENFORCED, OPT_ARG_ENFORCED),
	AST_APP_OPTION('o', OPTION_READONLY),
	AST_APP_OPTION('X', OPTION_EXIT),
	AST_APP_OPTION('s', OPTION_NOTECH),
	AST_APP_OPTION_ARG('n', OPTION_NAME, OPT_ARG_NAME),
	AST_APP_OPTION('d', OPTION_DTMF_SWITCH_MODES),
});

static int next_unique_id_to_use = 0;

struct chanspy_translation_helper {
	/* spy data */
	struct ast_audiohook spy_audiohook;
	struct ast_audiohook whisper_audiohook;
	struct ast_audiohook bridge_whisper_audiohook;
	int fd;
	int volfactor;
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

	if (ast_test_flag(&csth->spy_audiohook, OPTION_READONLY)) {
		/* Option 'o' was set, so don't mix channel audio */
		f = ast_audiohook_read_frame(&csth->spy_audiohook, samples, AST_AUDIOHOOK_DIRECTION_READ, AST_FORMAT_SLINEAR);
	} else {
		f = ast_audiohook_read_frame(&csth->spy_audiohook, samples, AST_AUDIOHOOK_DIRECTION_BOTH, AST_FORMAT_SLINEAR);
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

static int start_spying(struct ast_channel *chan, const char *spychan_name, struct ast_audiohook *audiohook)
{
	int res = 0;
	struct ast_channel *peer = NULL;

	ast_log(LOG_NOTICE, "Attaching %s to %s\n", spychan_name, chan->name);

	ast_set_flag(audiohook, AST_AUDIOHOOK_TRIGGER_SYNC | AST_AUDIOHOOK_SMALL_QUEUE);
	res = ast_audiohook_attach(chan, audiohook);

	if (!res && ast_test_flag(chan, AST_FLAG_NBRIDGE) && (peer = ast_bridged_channel(chan))) { 
		ast_softhangup(peer, AST_SOFTHANGUP_UNBRIDGE);
	}
	return res;
}

struct chanspy_ds {
	struct ast_channel *chan;
	char unique_id[20];
	ast_mutex_t lock;
};

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

static int channel_spy(struct ast_channel *chan, struct chanspy_ds *spyee_chanspy_ds, 
	int *volfactor, int fd, struct ast_flags *flags, char *exitcontext) 
{
	struct chanspy_translation_helper csth;
	int running = 0, res, x = 0;
	char inp[24] = {0};
	char *name;
	struct ast_frame *f;
	struct ast_silence_generator *silgen = NULL;
	struct ast_channel *spyee = NULL, *spyee_bridge = NULL;
	const char *spyer_name;

	ast_channel_lock(chan);
	spyer_name = ast_strdupa(chan->name);
	ast_channel_unlock(chan);

	ast_mutex_lock(&spyee_chanspy_ds->lock);
	while ((spyee = spyee_chanspy_ds->chan) && ast_channel_trylock(spyee)) {
		/* avoid a deadlock here, just in case spyee is masqueraded and
		 * chanspy_ds_chan_fixup() is called with the channel locked */
		DEADLOCK_AVOIDANCE(&spyee_chanspy_ds->lock);
	}
	ast_mutex_unlock(&spyee_chanspy_ds->lock);

	if (!spyee) {
		return 0;
	}

	/* We now hold the channel lock on spyee */

	if (ast_check_hangup(chan) || ast_check_hangup(spyee)) {
		ast_channel_unlock(spyee);
		return 0;
	}

	name = ast_strdupa(spyee->name);

	ast_verb(2, "Spying on channel %s\n", name);
	manager_event(EVENT_FLAG_CALL, "ChanSpyStart",
			"SpyerChannel: %s\r\n"
			"SpyeeChannel: %s\r\n",
			spyer_name, name);

	memset(&csth, 0, sizeof(csth));
	ast_copy_flags(&csth.spy_audiohook, flags, AST_FLAGS_ALL);

	ast_audiohook_init(&csth.spy_audiohook, AST_AUDIOHOOK_TYPE_SPY, "ChanSpy");

	if (start_spying(spyee, spyer_name, &csth.spy_audiohook)) {
		ast_audiohook_destroy(&csth.spy_audiohook);
		ast_channel_unlock(spyee);
		return 0;
	}

 	ast_audiohook_init(&csth.whisper_audiohook, AST_AUDIOHOOK_TYPE_WHISPER, "ChanSpy");
	ast_audiohook_init(&csth.bridge_whisper_audiohook, AST_AUDIOHOOK_TYPE_WHISPER, "Chanspy");
  	if (start_spying(spyee, spyer_name, &csth.whisper_audiohook)) {
		ast_log(LOG_WARNING, "Unable to attach whisper audiohook to spyee %s. Whisper mode disabled!\n", spyee->name);
	}
	if ((spyee_bridge = ast_bridged_channel(spyee))) {
		ast_channel_lock(spyee_bridge);
		if (start_spying(spyee_bridge, spyer_name, &csth.bridge_whisper_audiohook)) {
			ast_log(LOG_WARNING, "Unable to attach barge audiohook on spyee %s. Barge mode disabled!\n", spyee->name);
		}
		ast_channel_unlock(spyee_bridge);
	}
	ast_channel_unlock(spyee);
	spyee = NULL;

	ast_channel_lock(chan);
	ast_set_flag(chan, AST_FLAG_END_DTMF_ONLY);
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
	while ((res = ast_waitfor(chan, -1) > -1) && csth.spy_audiohook.status == AST_AUDIOHOOK_STATUS_RUNNING) {
		if (!(f = ast_read(chan)) || ast_check_hangup(chan)) {
			running = -1;
			break;
		}

		if (ast_test_flag(flags, OPTION_BARGE) && f->frametype == AST_FRAME_VOICE) {
			ast_audiohook_lock(&csth.whisper_audiohook);
			ast_audiohook_lock(&csth.bridge_whisper_audiohook);
			ast_audiohook_write_frame(&csth.whisper_audiohook, AST_AUDIOHOOK_DIRECTION_WRITE, f);
			ast_audiohook_write_frame(&csth.bridge_whisper_audiohook, AST_AUDIOHOOK_DIRECTION_WRITE, f);
			ast_audiohook_unlock(&csth.whisper_audiohook);
			ast_audiohook_unlock(&csth.bridge_whisper_audiohook);
			ast_frfree(f);
			continue;
		} else if (ast_test_flag(flags, OPTION_WHISPER) && f->frametype == AST_FRAME_VOICE) {
			ast_audiohook_lock(&csth.whisper_audiohook);
			ast_audiohook_write_frame(&csth.whisper_audiohook, AST_AUDIOHOOK_DIRECTION_WRITE, f);
			ast_audiohook_unlock(&csth.whisper_audiohook);
			ast_frfree(f);
			continue;
		}
		
		res = (f->frametype == AST_FRAME_DTMF) ? f->subclass : 0;
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

		if (res == '*') {
			running = 0;
			break;
		} else if (res == '#') {
			if (!ast_strlen_zero(inp)) {
				running = atoi(inp);
				break;
			}

			(*volfactor)++;
			if (*volfactor > 4)
				*volfactor = -4;
			ast_verb(3, "Setting spy volume on %s to %d\n", chan->name, *volfactor);

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
	ast_clear_flag(chan, AST_FLAG_END_DTMF_ONLY);
	ast_channel_unlock(chan);

	ast_audiohook_lock(&csth.whisper_audiohook);
	ast_audiohook_detach(&csth.whisper_audiohook);
	ast_audiohook_unlock(&csth.whisper_audiohook);
	ast_audiohook_destroy(&csth.whisper_audiohook);
	
	ast_audiohook_lock(&csth.bridge_whisper_audiohook);
	ast_audiohook_detach(&csth.bridge_whisper_audiohook);
	ast_audiohook_unlock(&csth.bridge_whisper_audiohook);
	ast_audiohook_destroy(&csth.bridge_whisper_audiohook);

	ast_audiohook_lock(&csth.spy_audiohook);
	ast_audiohook_detach(&csth.spy_audiohook);
	ast_audiohook_unlock(&csth.spy_audiohook);
	ast_audiohook_destroy(&csth.spy_audiohook);
	
	ast_verb(2, "Done Spying on channel %s\n", name);
	manager_event(EVENT_FLAG_CALL, "ChanSpyStop", "SpyeeChannel: %s\r\n", name);

	return running;
}

/*!
 * \note This relies on the embedded lock to be recursive, as it may be called
 * due to a call to chanspy_ds_free with the lock held there.
 */
static void chanspy_ds_destroy(void *data)
{
	struct chanspy_ds *chanspy_ds = data;

	/* Setting chan to be NULL is an atomic operation, but we don't want this
	 * value to change while this lock is held.  The lock is held elsewhere
	 * while it performs non-atomic operations with this channel pointer */

	ast_mutex_lock(&chanspy_ds->lock);
	chanspy_ds->chan = NULL;
	ast_mutex_unlock(&chanspy_ds->lock);
}

static void chanspy_ds_chan_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	struct chanspy_ds *chanspy_ds = data;
	
	ast_mutex_lock(&chanspy_ds->lock);
	chanspy_ds->chan = new_chan;
	ast_mutex_unlock(&chanspy_ds->lock);
}

static const struct ast_datastore_info chanspy_ds_info = {
	.type = "chanspy",
	.destroy = chanspy_ds_destroy,
	.chan_fixup = chanspy_ds_chan_fixup,
};

static struct chanspy_ds *chanspy_ds_free(struct chanspy_ds *chanspy_ds)
{
	struct ast_channel *chan;

	if (!chanspy_ds) {
		return NULL;
	}

	ast_mutex_lock(&chanspy_ds->lock);
	while ((chan = chanspy_ds->chan)) {
		struct ast_datastore *datastore;

		if (ast_channel_trylock(chan)) {
			DEADLOCK_AVOIDANCE(&chanspy_ds->lock);
			continue;
		}
		if ((datastore = ast_channel_datastore_find(chan, &chanspy_ds_info, chanspy_ds->unique_id))) {
			ast_channel_datastore_remove(chan, datastore);
			/* chanspy_ds->chan is NULL after this call */
			chanspy_ds_destroy(datastore->data);
			datastore->data = NULL;
			ast_datastore_free(datastore);
		}
		ast_channel_unlock(chan);
		break;
	}
	ast_mutex_unlock(&chanspy_ds->lock);

	return NULL;
}

/*! \note Returns the channel in the chanspy_ds locked as well as the chanspy_ds locked */
static struct chanspy_ds *setup_chanspy_ds(struct ast_channel *chan, struct chanspy_ds *chanspy_ds)
{
	struct ast_datastore *datastore = NULL;

	ast_mutex_lock(&chanspy_ds->lock);

	if (!(datastore = ast_datastore_alloc(&chanspy_ds_info, chanspy_ds->unique_id))) {
		ast_mutex_unlock(&chanspy_ds->lock);
		chanspy_ds = chanspy_ds_free(chanspy_ds);
		ast_channel_unlock(chan);
		return NULL;
	}
	
	chanspy_ds->chan = chan;
	datastore->data = chanspy_ds;
	ast_channel_datastore_add(chan, datastore);

	return chanspy_ds;
}

static struct chanspy_ds *next_channel(struct ast_channel *chan,
	const struct ast_channel *last, const char *spec,
	const char *exten, const char *context, struct chanspy_ds *chanspy_ds)
{
	struct ast_channel *next;
	const size_t pseudo_len = strlen("DAHDI/pseudo");

redo:
	if (!ast_strlen_zero(spec))
		next = ast_walk_channel_by_name_prefix_locked(last, spec, strlen(spec));
	else if (!ast_strlen_zero(exten))
		next = ast_walk_channel_by_exten_locked(last, exten, context);
	else
		next = ast_channel_walk_locked(last);

	if (!next)
		return NULL;

	if (!strncmp(next->name, "DAHDI/pseudo", pseudo_len)) {
		last = next;
		ast_channel_unlock(next);
		goto redo;
	} else if (next == chan) {
		last = next;
		ast_channel_unlock(next);
		goto redo;
	}

	return setup_chanspy_ds(next, chanspy_ds);
}

static int common_exec(struct ast_channel *chan, struct ast_flags *flags,
	int volfactor, const int fd, const char *mygroup, const char *myenforced,
	const char *spec, const char *exten, const char *context, const char *mailbox,
	const char *name_context)
{
	char nameprefix[AST_NAME_STRLEN];
	char peer_name[AST_NAME_STRLEN + 5];
	char exitcontext[AST_MAX_CONTEXT] = "";
	signed char zero_volume = 0;
	int waitms;
	int res;
	char *ptr;
	int num;
	int num_spyed_upon = 1;
	struct chanspy_ds chanspy_ds = { 0, };

	if (ast_test_flag(flags, OPTION_EXIT)) {
		const char *c;
		ast_channel_lock(chan);
		if ((c = pbx_builtin_getvar_helper(chan, "SPY_EXIT_CONTEXT"))) {
			ast_copy_string(exitcontext, c, sizeof(exitcontext));
		} else if (!ast_strlen_zero(chan->macrocontext)) {
			ast_copy_string(exitcontext, chan->macrocontext, sizeof(exitcontext));
		} else {
			ast_copy_string(exitcontext, chan->context, sizeof(exitcontext));
		}
		ast_channel_unlock(chan);
	}

	ast_mutex_init(&chanspy_ds.lock);

	snprintf(chanspy_ds.unique_id, sizeof(chanspy_ds.unique_id), "%d", ast_atomic_fetchadd_int(&next_unique_id_to_use, +1));

	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	ast_set_flag(chan, AST_FLAG_SPYING); /* so nobody can spy on us while we are spying */

	waitms = 100;

	for (;;) {
		struct chanspy_ds *peer_chanspy_ds = NULL, *next_chanspy_ds = NULL;
		struct ast_channel *prev = NULL, *peer = NULL;

		if (!ast_test_flag(flags, OPTION_QUIET) && num_spyed_upon) {
			res = ast_streamfile(chan, "beep", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");
			else if (res < 0) {
				ast_clear_flag(chan, AST_FLAG_SPYING);
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

		res = ast_waitfordigit(chan, waitms);
		if (res < 0) {
			ast_clear_flag(chan, AST_FLAG_SPYING);
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

		/* reset for the next loop around, unless overridden later */
		waitms = 100;
		num_spyed_upon = 0;

		for (peer_chanspy_ds = next_channel(chan, prev, spec, exten, context, &chanspy_ds);
		     peer_chanspy_ds;
			 chanspy_ds_free(peer_chanspy_ds), prev = peer,
		     peer_chanspy_ds = next_chanspy_ds ? next_chanspy_ds : 
			 	next_channel(chan, prev, spec, exten, context, &chanspy_ds), next_chanspy_ds = NULL) {
			int igrp = !mygroup;
			int ienf = !myenforced;
			char *s;

			peer = peer_chanspy_ds->chan;

			ast_mutex_unlock(&peer_chanspy_ds->lock);

			if (peer == prev) {
				ast_channel_unlock(peer);
				chanspy_ds_free(peer_chanspy_ds);
				break;
			}

			if (ast_check_hangup(chan)) {
				ast_channel_unlock(peer);
				chanspy_ds_free(peer_chanspy_ds);
				break;
			}

			if (ast_test_flag(flags, OPTION_BRIDGED) && !ast_bridged_channel(peer)) {
				ast_channel_unlock(peer);
				continue;
			}

			if (ast_check_hangup(peer) || ast_test_flag(peer, AST_FLAG_SPYING)) {
				ast_channel_unlock(peer);
				continue;
			}

			if (mygroup) {
				int num_groups = 0;
				int num_mygroups = 0;
				char dup_group[512];
				char dup_mygroup[512];
				char *groups[NUM_SPYGROUPS];
				char *mygroups[NUM_SPYGROUPS];
				const char *group;
				int x;
				int y;
				ast_copy_string(dup_mygroup, mygroup, sizeof(dup_mygroup));
				num_mygroups = ast_app_separate_args(dup_mygroup, ':', mygroups,
					ARRAY_LEN(mygroups));

				if ((group = pbx_builtin_getvar_helper(peer, "SPYGROUP"))) {
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
				ast_channel_unlock(peer);
				continue;
			}

			if (myenforced) {
				char ext[AST_CHANNEL_NAME + 3];
				char buffer[512];
				char *end;

				snprintf(buffer, sizeof(buffer) - 1, ":%s:", myenforced);

				ast_copy_string(ext + 1, peer->name, sizeof(ext) - 1);
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

			strcpy(peer_name, "spy-");
			strncat(peer_name, peer->name, AST_NAME_STRLEN - 4 - 1);
			ptr = strchr(peer_name, '/');
			*ptr++ = '\0';
			ptr = strsep(&ptr, "-");

			for (s = peer_name; s < ptr; s++)
				*s = tolower(*s);
			/* We have to unlock the peer channel here to avoid a deadlock.
			 * So, when we need to dereference it again, we have to lock the 
			 * datastore and get the pointer from there to see if the channel 
			 * is still valid. */
			ast_channel_unlock(peer);

			if (!ast_test_flag(flags, OPTION_QUIET)) {
				if (ast_test_flag(flags, OPTION_NAME)) {
					const char *local_context = S_OR(name_context, "default");
					const char *local_mailbox = S_OR(mailbox, ptr);
					res = ast_app_sayname(chan, local_mailbox, local_context);
				}
				if (!ast_test_flag(flags, OPTION_NAME) || res < 0) {
					if (!ast_test_flag(flags, OPTION_NOTECH)) {
						if (ast_fileexists(peer_name, NULL, NULL) > 0) {
							res = ast_streamfile(chan, peer_name, chan->language);
							if (!res) {
								res = ast_waitstream(chan, "");
							}
							if (res) {
								chanspy_ds_free(peer_chanspy_ds);
								break;
							}
						} else {
							res = ast_say_character_str(chan, peer_name, "", chan->language);
						}
					}
					if ((num = atoi(ptr)))
						ast_say_digits(chan, atoi(ptr), "", chan->language);
				}
			}

			res = channel_spy(chan, peer_chanspy_ds, &volfactor, fd, flags, exitcontext);
			num_spyed_upon++;	

			if (res == -1) {
				chanspy_ds_free(peer_chanspy_ds);
				goto exit;
			} else if (res == -2) {
				res = 0;
				chanspy_ds_free(peer_chanspy_ds);
				goto exit;
			} else if (res > 1 && spec) {
				struct ast_channel *next;

				snprintf(nameprefix, AST_NAME_STRLEN, "%s/%d", spec, res);

				if ((next = ast_get_channel_by_name_prefix_locked(nameprefix, strlen(nameprefix)))) {
					peer_chanspy_ds = chanspy_ds_free(peer_chanspy_ds);
					next_chanspy_ds = setup_chanspy_ds(next, &chanspy_ds);
				} else {
					/* stay on this channel, if it is still valid */

					ast_mutex_lock(&peer_chanspy_ds->lock);
					if (peer_chanspy_ds->chan) {
						ast_channel_lock(peer_chanspy_ds->chan);
						next_chanspy_ds = peer_chanspy_ds;
						peer_chanspy_ds = NULL;
					} else {
						/* the channel is gone */
						ast_mutex_unlock(&peer_chanspy_ds->lock);
						next_chanspy_ds = NULL;
					}
				}

				peer = NULL;
			}
		}
		if (res == -1 || ast_check_hangup(chan))
			break;
	}
exit:

	ast_clear_flag(chan, AST_FLAG_SPYING);

	ast_channel_setoption(chan, AST_OPTION_TXGAIN, &zero_volume, sizeof(zero_volume), 0);

	ast_mutex_lock(&chanspy_ds.lock);
	ast_mutex_unlock(&chanspy_ds.lock);
	ast_mutex_destroy(&chanspy_ds.lock);

	return res;
}

static int chanspy_exec(struct ast_channel *chan, void *data)
{
	char *myenforced = NULL;
	char *mygroup = NULL;
	char *recbase = NULL;
	int fd = 0;
	struct ast_flags flags;
	int oldwf = 0;
	int volfactor = 0;
	int res;
	char *mailbox = NULL;
	char *name_context = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(spec);
		AST_APP_ARG(options);
	);
	char *opts[OPT_ARG_ARRAY_SIZE];

	data = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, data);

	if (args.spec && !strcmp(args.spec, "all"))
		args.spec = NULL;

	if (args.options) {
		ast_app_parse_options(spy_opts, &flags, opts, args.options);
		if (ast_test_flag(&flags, OPTION_GROUP))
			mygroup = opts[OPT_ARG_GROUP];

		if (ast_test_flag(&flags, OPTION_RECORD) &&
			!(recbase = opts[OPT_ARG_RECORD]))
			recbase = "chanspy";

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


	} else
		ast_clear_flag(&flags, AST_FLAGS_ALL);

	oldwf = chan->writeformat;
	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR) < 0) {
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

	res = common_exec(chan, &flags, volfactor, fd, mygroup, myenforced, args.spec, NULL, NULL, mailbox, name_context);

	if (fd)
		close(fd);

	if (oldwf && ast_set_write_format(chan, oldwf) < 0)
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");

	return res;
}

static int extenspy_exec(struct ast_channel *chan, void *data)
{
	char *ptr, *exten = NULL;
	char *mygroup = NULL;
	char *recbase = NULL;
	int fd = 0;
	struct ast_flags flags;
	int oldwf = 0;
	int volfactor = 0;
	int res;
	char *mailbox = NULL;
	char *name_context = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(context);
		AST_APP_ARG(options);
	);

	data = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, data);
	if (!ast_strlen_zero(args.context) && (ptr = strchr(args.context, '@'))) {
		exten = args.context;
		*ptr++ = '\0';
		args.context = ptr;
	}

	if (ast_strlen_zero(args.context))
		args.context = ast_strdupa(chan->context);

	if (args.options) {
		char *opts[OPT_ARG_ARRAY_SIZE];

		ast_app_parse_options(spy_opts, &flags, opts, args.options);
		if (ast_test_flag(&flags, OPTION_GROUP))
			mygroup = opts[OPT_ARG_GROUP];

		if (ast_test_flag(&flags, OPTION_RECORD) &&
			!(recbase = opts[OPT_ARG_RECORD]))
			recbase = "chanspy";

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

	} else
		ast_clear_flag(&flags, AST_FLAGS_ALL);

	oldwf = chan->writeformat;
	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR) < 0) {
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


	res = common_exec(chan, &flags, volfactor, fd, mygroup, NULL, NULL, exten, args.context, mailbox, name_context);

	if (fd)
		close(fd);

	if (oldwf && ast_set_write_format(chan, oldwf) < 0)
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");

	return res;
}

static int unload_module(void)
{
	int res = 0;

	res |= ast_unregister_application(app_chan);
	res |= ast_unregister_application(app_ext);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_register_application_xml(app_chan, chanspy_exec);
	res |= ast_register_application_xml(app_ext, extenspy_exec);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Listen to the audio of an active channel");
