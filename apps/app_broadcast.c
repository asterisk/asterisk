/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Naveen Albert
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
 * \brief Channel audio broadcasting
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <ctype.h>
#include <errno.h>

#include "asterisk/channel.h"
#include "asterisk/audiohook.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/options.h"
#include "asterisk/autochan.h"
#include "asterisk/format_cache.h"
#include "asterisk/cli.h" /* use ESS macro */

/*** DOCUMENTATION
	<application name="Broadcast" language="en_US">
		<synopsis>
			Transmit or receive audio to or from multiple channels simultaneously
		</synopsis>
		<syntax>
			<parameter name="options">
				<optionlist>
					<option name="b">
						<para>In addition to broadcasting to target channels, also
						broadcast to any channels to which target channels are bridged.</para>
					</option>
					<option name="l">
						<para>Allow usage of a long queue to store audio frames.</para>
						<note><para>This may introduce some delay in the received audio feed, but will improve the audio quality.</para></note>
					</option>
					<option name="o">
						<para>Do not mix streams when combining audio from target channels (only applies with s option).</para>
					</option>
					<option name="r">
						<para>Feed frames to barge channels in "reverse" by injecting them into the primary channel's read queue instead.</para>
						<para>This option is required for barge to work in a n-party bridge (but not for 2-party bridges). Alternately, you
						can add an intermediate channel by using a non-optimized Local channel, so that the target channel is bridged with
						a single channel that is connected to the bridge, but it is recommended this option be used instead.</para>
						<para>Note that this option will always feed injected audio to the other party, regardless of whether the target
						channel is bridged or not.</para>
					</option>
					<option name="s">
						<para>Rather than broadcast audio to a bunch of channels, receive the combined audio from the target channels.</para>
					</option>
					<option name="w">
						<para>Broadcast audio received on this channel to other channels.</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="channels" required="true" argsep=",">
				<para>List of channels for broadcast targets.</para>
				<para>Channel names must be the full channel names, not merely device names.</para>
				<para>Broadcasting will continue until the broadcasting channel hangs up or all target channels have hung up.</para>
			</parameter>
		</syntax>
		<description>
			<para>This application can be used to broadcast audio to multiple channels at once.
			Any audio received on this channel will be transmitted to all of the specified channels and, optionally, their bridged peers.</para>
			<para>It can also be used to aggregate audio from multiple channels at once.
			Any audio on any of the specified channels, and optionally their bridged peers, will be transmitted to this channel.</para>
			<para>Execution of the application continues until either the broadcasting channel hangs up
			or all specified channels have hung up.</para>
			<para>This application is used for one-to-many and many-to-one audio applications where
			bridge mixing cannot be done synchronously on all the involved channels.
			This is primarily useful for injecting the same audio stream into multiple channels at once,
			or doing the reverse, combining the audio from multiple channels into a single stream.
			This contrasts with using a separate injection channel for each target channel and/or
			using a conference bridge.</para>
			<para>The channel running the Broadcast application must do so synchronously. The specified channels,
			however, may be doing other things.</para>
			<example title="Broadcast received audio to three channels and their bridged peers">
			same => n,Broadcast(wb,DAHDI/1,DAHDI/3,PJSIP/doorphone)
			</example>
			<example title="Broadcast received audio to three channels, only">
			same => n,Broadcast(w,DAHDI/1,DAHDI/3,PJSIP/doorphone)
			</example>
			<example title="Combine audio from three channels and their bridged peers to us">
			same => n,Broadcast(s,DAHDI/1,DAHDI/3,PJSIP/doorphone)
			</example>
			<example title="Combine audio from three channels to us">
			same => n,Broadcast(so,DAHDI/1,DAHDI/3,PJSIP/doorphone)
			</example>
			<example title="Two-way audio with a bunch of channels">
			same => n,Broadcast(wbso,DAHDI/1,DAHDI/3,PJSIP/doorphone)
			</example>
			<para>Note that in the last example above, this is NOT the same as a conference bridge.
			The specified channels are not audible to each other, only to the channel running the
			Broadcast application. The two-way audio is only between the broadcasting channel and
			each of the specified channels, individually.</para>
		</description>
		<see-also>
			<ref type="application">ChanSpy</ref>
		</see-also>
	</application>
 ***/

static const char app_broadcast[] = "Broadcast";

enum {
	OPTION_READONLY          = (1 << 0),    /* Don't mix the two channels */
	OPTION_BARGE             = (1 << 1),    /* Barge mode (whisper to both channels) */
	OPTION_LONG_QUEUE        = (1 << 2),	/* Allow usage of a long queue to store audio frames. */
	OPTION_WHISPER           = (1 << 3),
	OPTION_SPY               = (1 << 4),
	OPTION_REVERSE_FEED      = (1 << 5),
	OPTION_ANSWER_WARN       = (1 << 6),	/* Internal flag, not set by user */
};

AST_APP_OPTIONS(spy_opts, {
	AST_APP_OPTION('b', OPTION_BARGE),
	AST_APP_OPTION('l', OPTION_LONG_QUEUE),
	AST_APP_OPTION('o', OPTION_READONLY),
	AST_APP_OPTION('r', OPTION_REVERSE_FEED),
	AST_APP_OPTION('s', OPTION_SPY),
	AST_APP_OPTION('w', OPTION_WHISPER),
});

struct multi_autochan {
	char *name;
	struct ast_autochan *autochan;
	struct ast_autochan *bridge_autochan;
	struct ast_audiohook whisper_audiohook;
	struct ast_audiohook bridge_whisper_audiohook;
	struct ast_audiohook spy_audiohook;
	unsigned int connected:1;
	unsigned int bridge_connected:1;
	unsigned int spying:1;
	AST_LIST_ENTRY(multi_autochan) entry;	/*!< Next record */
};

AST_RWLIST_HEAD(multi_autochan_list, multi_autochan);

struct multi_spy {
	struct multi_autochan_list *chanlist;
	unsigned int readonly:1;
};

static void *spy_alloc(struct ast_channel *chan, void *data)
{
	return data; /* just store the data pointer in the channel structure */
}

static void spy_release(struct ast_channel *chan, void *data)
{
	return; /* nothing to do */
}

static int spy_generate(struct ast_channel *chan, void *data, int len, int samples)
{
	struct multi_spy *multispy = data;
	struct multi_autochan_list *chanlist = multispy->chanlist;
	struct multi_autochan *mac;
	struct ast_frame *f;
	short *data1, *data2;
	int res, i;

	/* All the frames we get are slin, so they will all have the same number of samples. */
	static const int num_samples = 160;
	short combine_buf[num_samples];
	struct ast_frame wf = {
		.frametype = AST_FRAME_VOICE,
		.offset = 0,
		.subclass.format = ast_format_slin,
		.datalen = num_samples * 2,
		.samples = num_samples,
		.src = __FUNCTION__,
	};

	memset(&combine_buf, 0, sizeof(combine_buf));
	wf.data.ptr = combine_buf;

	AST_RWLIST_WRLOCK(chanlist);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(chanlist, mac, entry) {
		ast_audiohook_lock(&mac->spy_audiohook);
		if (mac->spy_audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING) {
			ast_audiohook_unlock(&mac->spy_audiohook); /* Channel is already gone more than likely, the broadcasting channel will clean this up. */
			continue;
		}

		if (multispy->readonly) { /* Option 'o' was set, so don't mix channel audio */
			f = ast_audiohook_read_frame(&mac->spy_audiohook, samples, AST_AUDIOHOOK_DIRECTION_READ, ast_format_slin);
		} else {
			f = ast_audiohook_read_frame(&mac->spy_audiohook, samples, AST_AUDIOHOOK_DIRECTION_BOTH, ast_format_slin);
		}
		ast_audiohook_unlock(&mac->spy_audiohook);

		if (!f) {
			continue; /* No frame? No problem. */
		}

		/* Mix the samples. */
		for (i = 0, data1 = combine_buf, data2 = f->data.ptr; i < num_samples; i++, data1++, data2++) {
			ast_slinear_saturated_add(data1, data2);
		}
		ast_frfree(f);
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(chanlist);

	res = ast_write(chan, &wf);
	ast_frfree(&wf);

	return res;
}

static struct ast_generator spygen = {
	.alloc = spy_alloc,
	.release = spy_release,
	.generate = spy_generate,
};

static int start_spying(struct ast_autochan *autochan, const char *spychan_name, struct ast_audiohook *audiohook, struct ast_flags *flags)
{
	int res;

	ast_autochan_channel_lock(autochan);
	ast_debug(1, "Attaching spy channel %s to %s\n", spychan_name, ast_channel_name(autochan->chan));

	if (ast_test_flag(flags, OPTION_READONLY)) {
		ast_set_flag(audiohook, AST_AUDIOHOOK_MUTE_WRITE);
	} else {
		ast_set_flag(audiohook, AST_AUDIOHOOK_TRIGGER_SYNC);
	}
	if (ast_test_flag(flags, OPTION_LONG_QUEUE)) {
		ast_debug(2, "Using a long queue to store audio frames in spy audiohook\n");
	} else {
		ast_set_flag(audiohook, AST_AUDIOHOOK_SMALL_QUEUE);
	}
	res = ast_audiohook_attach(autochan->chan, audiohook);
	ast_autochan_channel_unlock(autochan);
	return res;
}

static int attach_barge(struct ast_autochan *spyee_autochan, struct ast_autochan **spyee_bridge_autochan,
	struct ast_audiohook *bridge_whisper_audiohook, const char *spyer_name, const char *name, struct ast_flags *flags)
{
	int retval = 0;
	struct ast_autochan *internal_bridge_autochan;
	struct ast_channel *spyee_chan;
	RAII_VAR(struct ast_channel *, bridged, NULL, ast_channel_cleanup);

	ast_autochan_channel_lock(spyee_autochan);
	spyee_chan = ast_channel_ref(spyee_autochan->chan);
	ast_autochan_channel_unlock(spyee_autochan);

	/* Note that ast_channel_bridge_peer only returns non-NULL for 2-party bridges, not n-party bridges (e.g. ConfBridge) */
	bridged = ast_channel_bridge_peer(spyee_chan);
	ast_channel_unref(spyee_chan);
	if (!bridged) {
		ast_debug(9, "Channel %s is not yet bridged, unable to setup barge\n", ast_channel_name(spyee_chan));
		/* If we're bridged, but it's not a 2-party bridge, then we probably should have used OPTION_REVERSE_FEED. */
		if (ast_test_flag(flags, OPTION_ANSWER_WARN) && ast_channel_is_bridged(spyee_chan)) {
			ast_clear_flag(flags, OPTION_ANSWER_WARN); /* Don't warn more than once. */
			ast_log(LOG_WARNING, "Barge failed: channel is bridged, but not to a 2-party bridge. Use the 'r' option.\n");
		}
		return -1;
	}

	ast_audiohook_init(bridge_whisper_audiohook, AST_AUDIOHOOK_TYPE_WHISPER, "Broadcast", 0);
	internal_bridge_autochan = ast_autochan_setup(bridged);
	if (!internal_bridge_autochan) {
		return -1;
	}

	if (start_spying(internal_bridge_autochan, spyer_name, bridge_whisper_audiohook, flags)) {
		ast_log(LOG_WARNING, "Unable to attach barge audiohook on spyee '%s'. Barge mode disabled.\n", name);
		retval = -1;
	}

	*spyee_bridge_autochan = internal_bridge_autochan;
	return retval;
}

static void multi_autochan_free(struct multi_autochan *mac)
{
	if (mac->connected) {
		if (mac->whisper_audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING) {
			ast_debug(2, "Whisper audiohook no longer running\n");
		}
		ast_audiohook_lock(&mac->whisper_audiohook);
		ast_audiohook_detach(&mac->whisper_audiohook);
		ast_audiohook_unlock(&mac->whisper_audiohook);
		ast_audiohook_destroy(&mac->whisper_audiohook);
	}
	if (mac->bridge_connected) {
		if (mac->bridge_whisper_audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING) {
			ast_debug(2, "Whisper (bridged) audiohook no longer running\n");
		}
		ast_audiohook_lock(&mac->bridge_whisper_audiohook);
		ast_audiohook_detach(&mac->bridge_whisper_audiohook);
		ast_audiohook_unlock(&mac->bridge_whisper_audiohook);
		ast_audiohook_destroy(&mac->bridge_whisper_audiohook);
	}
	if (mac->spying) {
		if (mac->spy_audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING) {
			ast_debug(2, "Spy audiohook no longer running\n");
		}
		ast_audiohook_lock(&mac->spy_audiohook);
		ast_audiohook_detach(&mac->spy_audiohook);
		ast_audiohook_unlock(&mac->spy_audiohook);
		ast_audiohook_destroy(&mac->spy_audiohook);
	}
	if (mac->name) {
		int total = mac->connected + mac->bridge_connected + mac->spying;
		ast_debug(1, "Removing channel %s from target list (%d hook%s)\n", mac->name, total, ESS(total));
		ast_free(mac->name);
	}
	if (mac->autochan) {
		ast_autochan_destroy(mac->autochan);
	}
	if (mac->bridge_autochan) {
		ast_autochan_destroy(mac->bridge_autochan);
	}
	ast_free(mac);
}

static int do_broadcast(struct ast_channel *chan, struct ast_flags *flags, const char *channels)
{
	int res = 0;
	struct ast_frame *f;
	struct ast_silence_generator *silgen = NULL;
	struct multi_spy multispy;
	struct multi_autochan_list chanlist;
	struct multi_autochan *mac;
	int numchans = 0;
	int readonly = ast_test_flag(flags, OPTION_READONLY) ? 1 : 0;
	char *next, *chansdup = ast_strdupa(channels);

	AST_RWLIST_HEAD_INIT(&chanlist);
	ast_channel_set_flag(chan, AST_FLAG_SPYING);

	ast_set_flag(flags, OPTION_ANSWER_WARN); /* Initialize answer warn to 1 */

	/* Hey, look ma, no list lock needed! Sometimes, it's nice to not have to share... */

	/* Build a list of targets */
	while ((next = strsep(&chansdup, ","))) {
		struct ast_channel *ochan;
		if (ast_strlen_zero(next)) {
			continue;
		}
		if (!strcmp(next, ast_channel_name(chan))) {
			ast_log(LOG_WARNING, "Refusing to broadcast to ourself: %s\n", next);
			continue;
		}
		ochan = ast_channel_get_by_name(next);
		if (!ochan) {
			ast_log(LOG_WARNING, "No such channel: %s\n", next);
			continue;
		}
		/* Append to end of list. */
		if (!(mac = ast_calloc(1, sizeof(*mac)))) {
			ast_log(LOG_WARNING, "Multi autochan allocation failure\n");
			continue;
		}
		mac->name = ast_strdup(next);
		mac->autochan = ast_autochan_setup(ochan);
		if (!mac->name || !mac->autochan) {
			multi_autochan_free(mac);
			continue;
		}
		if (ast_test_flag(flags, OPTION_WHISPER)) {
			mac->connected = 1;
			ast_audiohook_init(&mac->whisper_audiohook, AST_AUDIOHOOK_TYPE_WHISPER, "Broadcast", 0);
			/* Inject audio from our channel to this target. */
			if (start_spying(mac->autochan, next, &mac->whisper_audiohook, flags)) {
				ast_log(LOG_WARNING, "Unable to attach whisper audiohook to %s\n", next);
				multi_autochan_free(mac);
				continue;
			}
		}
		if (ast_test_flag(flags, OPTION_SPY)) {
			mac->spying = 1;
			ast_audiohook_init(&mac->spy_audiohook, AST_AUDIOHOOK_TYPE_SPY, "Broadcast", 0);
			if (start_spying(mac->autochan, next, &mac->spy_audiohook, flags)) {
				ast_log(LOG_WARNING, "Unable to attach spy audiohook to %s\n", next);
				multi_autochan_free(mac);
				continue;
			}
		}
		AST_RWLIST_INSERT_TAIL(&chanlist, mac, entry);
		numchans++;
		ochan = ast_channel_unref(ochan);
	}

	ast_verb(4, "Broadcasting to %d channel%s on %s\n", numchans, ESS(numchans), ast_channel_name(chan));
	ast_debug(1, "Broadcasting: (TX->1) whisper=%d, (TX->2) barge=%d, (RX<-%d) spy=%d (%s)\n",
		ast_test_flag(flags, OPTION_WHISPER) ? 1 : 0,
		ast_test_flag(flags, OPTION_BARGE) ? 1 : 0,
		readonly ? 1 : 2,
		ast_test_flag(flags, OPTION_SPY) ? 1 : 0,
		readonly ? "single" : "both");

	if (ast_test_flag(flags, OPTION_SPY)) {
		multispy.chanlist = &chanlist;
		multispy.readonly = readonly;
		ast_activate_generator(chan, &spygen, &multispy);
	} else {
		/* We're not expecting to read any audio, just broadcast audio to a bunch of other channels. */
		silgen = ast_channel_start_silence_generator(chan);
	}

	while (numchans && ast_waitfor(chan, -1) > 0) {
		int fres = 0;
		f = ast_read(chan);
		if (!f) {
			ast_debug(1, "Channel %s must have hung up\n", ast_channel_name(chan));
			res = -1;
			break;
		}
		if (f->frametype != AST_FRAME_VOICE) { /* Ignore any non-voice frames */
			ast_frfree(f);
			continue;
		}
		/* Write the frame to all our targets. */
		AST_RWLIST_WRLOCK(&chanlist);
		AST_RWLIST_TRAVERSE_SAFE_BEGIN(&chanlist, mac, entry) {
			/* Note that if no media is received, execution is suspended, but assuming continuous or
			 * or frequent audio on the broadcasting channel, we'll quickly enough detect hung up targets.
			 * This isn't really an issue, just something that might be confusing at first, but this is
			 * due to the limitation with audiohooks of using the channel for timing. */
			if ((ast_test_flag(flags, OPTION_WHISPER) && mac->whisper_audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING)
				|| (ast_test_flag(flags, OPTION_SPY) && mac->spy_audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING)
				|| (mac->bridge_connected && ast_test_flag(flags, OPTION_BARGE) && mac->bridge_whisper_audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING)) {
				/* Even if we're spying only and not actually broadcasting audio, we need to detect channel hangup. */
				AST_RWLIST_REMOVE_CURRENT(entry);
				ast_debug(2, "Looks like %s has hung up\n", mac->name);
				multi_autochan_free(mac);
				numchans--;
				ast_debug(2, "%d channel%s remaining in broadcast on %s\n", numchans, ESS(numchans), ast_channel_name(chan));
				continue;
			}

			if (ast_test_flag(flags, OPTION_WHISPER)) {
				ast_audiohook_lock(&mac->whisper_audiohook);
				fres |= ast_audiohook_write_frame(&mac->whisper_audiohook, AST_AUDIOHOOK_DIRECTION_WRITE, f);
				ast_audiohook_unlock(&mac->whisper_audiohook);
			}

			if (ast_test_flag(flags, OPTION_BARGE)) {
				/* This hook lets us inject audio into the channel that the spyee is currently
				 * bridged with. If the spyee isn't bridged with anything yet, nothing will
				 * be attached and we'll need to continue attempting to attach the barge
				 * audio hook.
				 * The exception to this is if we are emulating barge by doing it "directly",
				 * that is injecting the frames onto this channel's read queue, rather than
				 * its bridged peer's write queue, then skip this. We only do one or the other. */
				if (!ast_test_flag(flags, OPTION_REVERSE_FEED) && !mac->bridge_connected && !attach_barge(mac->autochan, &mac->bridge_autochan,
						&mac->bridge_whisper_audiohook, ast_channel_name(chan), mac->name, flags)) {
					ast_debug(2, "Attached barge channel for %s\n", mac->name);
					mac->bridge_connected = 1;
				}

				if (mac->bridge_connected) {
					ast_audiohook_lock(&mac->bridge_whisper_audiohook);
					fres |= ast_audiohook_write_frame(&mac->bridge_whisper_audiohook, AST_AUDIOHOOK_DIRECTION_WRITE, f);
					ast_audiohook_unlock(&mac->bridge_whisper_audiohook);
				} else if (ast_test_flag(flags, OPTION_REVERSE_FEED)) {
					/* So, this is really clever...
					 * If we're connected to an n-party bridge instead of a 2-party bridge,
					 * attach_barge will ALWAYS fail because we're connected to a bridge, not
					 * a single peer channel.
					 * Recall that the objective is for injected audio to be audible to both
					 * sides of the channel. So really, the typical way of doing this by
					 * directly injecting frames separately onto both channels is kind of
					 * bizarre to begin with, when you think about it.
					 *
					 * In other words, this is how ChanSpy and this module by default work:
					 * We have audio F to inject onto channels A and B, which are <= bridged =>:
					 * READ <- A -> WRITE <==> READ <- B -> WRITE
					 *            F --^                  F --^
					 *
					 * So that makes the same audio audible to both channels A and B, but
					 * in kind of a roundabout way. What if the bridged peer changes at
					 * some point, for example?
					 *
					 * While that method works for 2-party bridges, it doesn't work at all
					 * for an n-party bridge, so we do the thing that seems obvious to begin with:
					 * dump the frames onto THIS channel's read queue, and the channels will
					 * make their way into the bridge like any other audio from this channel,
					 * and everything just works perfectly, no matter what kind of bridging
					 * scenario is being used. At that point, we don't even care if we're
					 * bridged or not, and really, why should we?
					 *
					 * In other words, we do this:
					 * READ <- A -> WRITE <==> READ <- B -> WRITE
					 *                       F --^       F --^
					 */
					ast_audiohook_lock(&mac->whisper_audiohook);
					fres |= ast_audiohook_write_frame(&mac->whisper_audiohook, AST_AUDIOHOOK_DIRECTION_READ, f);
					ast_audiohook_unlock(&mac->whisper_audiohook);
				}
			}
			if (fres) {
				ast_log(LOG_WARNING, "Failed to write to audiohook for %s\n", mac->name);
				fres = 0;
			}
		}
		AST_RWLIST_TRAVERSE_SAFE_END;
		AST_RWLIST_UNLOCK(&chanlist);
		ast_frfree(f);
	}

	if (!numchans) {
		ast_debug(1, "Exiting due to all target channels having left the broadcast\n");
	}

	if (ast_test_flag(flags, OPTION_SPY)) {
		ast_deactivate_generator(chan);
	} else {
		ast_channel_stop_silence_generator(chan, silgen);
	}

	/* Cleanup any remaining targets */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&chanlist, mac, entry) {
		AST_RWLIST_REMOVE_CURRENT(entry);
		multi_autochan_free(mac);
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	ast_channel_clear_flag(chan, AST_FLAG_SPYING);
	return res;
}

static int broadcast_exec(struct ast_channel *chan, const char *data)
{
	struct ast_flags flags;
	struct ast_format *write_format;
	int res = -1;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(options);
		AST_APP_ARG(channels); /* Channel list last, so we can have multiple */
	);
	char *parse = NULL;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Broadcast requires at least one channel\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.channels)) {
		ast_log(LOG_WARNING, "Must specify at least one channel for broadcast\n");
		return -1;
	}
	if (args.options) {
		ast_app_parse_options(spy_opts, &flags, NULL, args.options);
	} else {
		ast_clear_flag(&flags, AST_FLAGS_ALL);
	}

	if (!ast_test_flag(&flags, OPTION_BARGE) && !ast_test_flag(&flags, OPTION_SPY) && !ast_test_flag(&flags, OPTION_WHISPER)) {
		ast_log(LOG_WARNING, "At least one of the b, s, or w option must be specified (provided options have no effect)\n");
		return -1;
	}

	write_format = ao2_bump(ast_channel_writeformat(chan));
	if (ast_set_write_format(chan, ast_format_slin) < 0) {
		ast_log(LOG_ERROR, "Failed to set write format to slin.\n");
		goto cleanup;
	}

	res = do_broadcast(chan, &flags, args.channels);

	/* Restore previous write format */
	if (ast_set_write_format(chan, write_format)) {
		ast_log(LOG_ERROR, "Failed to restore write format for channel %s\n", ast_channel_name(chan));
	}

cleanup:
	ao2_ref(write_format, -1);
	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app_broadcast);
}

static int load_module(void)
{
	return ast_register_application_xml(app_broadcast, broadcast_exec);
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Channel Audio Broadcasting");
