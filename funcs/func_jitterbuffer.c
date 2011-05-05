/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief Put a jitterbuffer on the read side of a channel
 *
 * \author David Vossel <dvossel@digium.com>
 *
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/framehook.h"
#include "asterisk/pbx.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/timing.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<function name="JITTERBUFFER" language="en_US">
		<synopsis>
			Add a Jitterbuffer to the Read side of the channel.  This dejitters the audio stream before it reaches the Asterisk core. This is a write only function.
		</synopsis>
		<syntax>
			<parameter name="jitterbuffer type" required="true">
				<para>Jitterbuffer type can be either <literal>fixed</literal> or <literal>adaptive</literal>.</para>
				<para>Used as follows. </para>
				<para>Set(JITTERBUFFER(type)=max_size[,resync_threshold[,target_extra]])</para>
				<para>Set(JITTERBUFFER(type)=default) </para>
			</parameter>
		</syntax>
		<description>
			<para>max_size: Defaults to 200 ms</para>
			<para>Length in milliseconds of buffer.</para>
			<para> </para>
			<para>resync_threshold: Defaults to 1000ms </para>
			<para>The length in milliseconds over which a timestamp difference will result in resyncing the jitterbuffer. </para>
			<para> </para>
			<para>target_extra: Defaults to 40ms</para>
			<para>This option only affects the adaptive jitterbuffer. It represents the amount time in milliseconds by which the new jitter buffer will pad its size.</para>
			<para> </para>
			<para>Examples:</para>
			<para>exten => 1,1,Set(JITTERBUFFER(fixed)=default);Fixed with defaults. </para>
			<para>exten => 1,1,Set(JITTERBUFFER(fixed)=200);Fixed with max size 200ms, default resync threshold and target extra. </para>
			<para>exten => 1,1,Set(JITTERBUFFER(fixed)=200,1500);Fixed with max size 200ms resync threshold 1500. </para>
			<para>exten => 1,1,Set(JITTERBUFFER(adaptive)=default);Adaptive with defaults. </para>
			<para>exten => 1,1,Set(JITTERBUFFER(adaptive)=200,,60);Adaptive with max size 200ms, default resync threshold and 40ms target extra. </para>
		</description>
	</function>
 ***/

#define DEFAULT_TIMER_INTERVAL 20
#define DEFAULT_SIZE  200
#define DEFAULT_TARGET_EXTRA  40
#define DEFAULT_RESYNC  1000
#define DEFAULT_TYPE AST_JB_FIXED

struct jb_framedata {
	const struct ast_jb_impl *jb_impl;
	struct ast_jb_conf jb_conf;
	struct timeval start_tv;
	struct ast_format last_format;
	struct ast_timer *timer;
	int timer_interval; /* ms between deliveries */
	int timer_fd;
	int first;
	void *jb_obj;
};

static void jb_framedata_destroy(struct jb_framedata *framedata)
{
	if (framedata->timer) {
		ast_timer_close(framedata->timer);
		framedata->timer = NULL;
	}
	if (framedata->jb_impl && framedata->jb_obj) {
		struct ast_frame *f;
		while (framedata->jb_impl->remove(framedata->jb_obj, &f) == AST_JB_IMPL_OK) {
			ast_frfree(f);
		}
		framedata->jb_impl->destroy(framedata->jb_obj);
		framedata->jb_obj = NULL;
	}
	ast_free(framedata);
}

static void jb_conf_default(struct ast_jb_conf *conf)
{
	conf->max_size = DEFAULT_SIZE;
	conf->resync_threshold = DEFAULT_RESYNC;
	ast_copy_string(conf->impl, "fixed", sizeof(conf->impl));
	conf->target_extra = DEFAULT_TARGET_EXTRA;
}

/* set defaults */
static int jb_framedata_init(struct jb_framedata *framedata, const char *data, const char *value)
{
	int jb_impl_type = DEFAULT_TYPE;

	/* Initialize defaults */
	framedata->timer_fd = -1;
	jb_conf_default(&framedata->jb_conf);
	if (!(framedata->jb_impl = ast_jb_get_impl(jb_impl_type))) {
		return -1;
	}
	if (!(framedata->timer = ast_timer_open())) {
		return -1;
	}
	framedata->timer_fd = ast_timer_fd(framedata->timer);
	framedata->timer_interval = DEFAULT_TIMER_INTERVAL;
	ast_timer_set_rate(framedata->timer, 1000 / framedata->timer_interval);
	framedata->start_tv = ast_tvnow();



	/* Now check user options to see if any of the defaults need to change. */
	if (!ast_strlen_zero(data)) {
		if (!strcasecmp(data, "fixed")) {
			jb_impl_type = AST_JB_FIXED;
		} else if (!strcasecmp(data, "adaptive")) {
			jb_impl_type = AST_JB_ADAPTIVE;
		} else {
			ast_log(LOG_WARNING, "Unknown Jitterbuffer type %s. Failed to create jitterbuffer.\n", data);
			return -1;
		}
		ast_copy_string(framedata->jb_conf.impl, data, sizeof(framedata->jb_conf.impl));
	}

	if (!ast_strlen_zero(value) && strcasecmp(value, "default")) {
		char *parse = ast_strdupa(value);
		int res = 0;
		AST_DECLARE_APP_ARGS(args,
			AST_APP_ARG(max_size);
			AST_APP_ARG(resync_threshold);
			AST_APP_ARG(target_extra);
		);

		AST_STANDARD_APP_ARGS(args, parse);
		if (!ast_strlen_zero(args.max_size)) {
			res |= ast_jb_read_conf(&framedata->jb_conf,
				"jbmaxsize",
				args.max_size);
		}
		if (!ast_strlen_zero(args.resync_threshold)) {
			res |= ast_jb_read_conf(&framedata->jb_conf,
				"jbresyncthreshold",
				args.resync_threshold);
		}
		if (!ast_strlen_zero(args.target_extra)) {
			res |= ast_jb_read_conf(&framedata->jb_conf,
				"jbtargetextra",
				args.target_extra);
		}
		if (res) {
			ast_log(LOG_WARNING, "Invalid jitterbuffer parameters %s\n", value);
		}
	}

	/* now that all the user parsing is done and nothing will change, create the jb obj */
	framedata->jb_obj = framedata->jb_impl->create(&framedata->jb_conf, framedata->jb_conf.resync_threshold);
	return 0;
}

static void datastore_destroy_cb(void *data) {
	ast_free(data);
	ast_debug(1, "JITTERBUFFER datastore destroyed\n");
}

static const struct ast_datastore_info jb_datastore = {
	.type = "jitterbuffer",
	.destroy = datastore_destroy_cb
};

static void hook_destroy_cb(void *framedata)
{
	ast_debug(1, "JITTERBUFFER hook destroyed\n");
	jb_framedata_destroy((struct jb_framedata *) framedata);
}

static struct ast_frame *hook_event_cb(struct ast_channel *chan, struct ast_frame *frame, enum ast_framehook_event event, void *data)
{
	struct jb_framedata *framedata = data;
	struct timeval now_tv;
	unsigned long now;
	int putframe = 0; /* signifies if audio frame was placed into the buffer or not */

	switch (event) {
	case AST_FRAMEHOOK_EVENT_READ:
		break;
	case AST_FRAMEHOOK_EVENT_ATTACHED:
	case AST_FRAMEHOOK_EVENT_DETACHED:
	case AST_FRAMEHOOK_EVENT_WRITE:
		return frame;
	}

	if (chan->fdno == AST_JITTERBUFFER_FD && framedata->timer) {
		ast_timer_ack(framedata->timer, 1);
	}

	if (!frame) {
		return frame;
	}

	now_tv = ast_tvnow();
	now = ast_tvdiff_ms(now_tv, framedata->start_tv);

	if (frame->frametype == AST_FRAME_VOICE) {
		int res;
		struct ast_frame *jbframe;

		if (!ast_test_flag(frame, AST_FRFLAG_HAS_TIMING_INFO) || frame->len < 2 || frame->ts < 0) {
			/* only frames with timing info can enter the jitterbuffer */
			return frame;
		}

		jbframe = ast_frisolate(frame);
		ast_format_copy(&framedata->last_format, &frame->subclass.format);

		if (frame->len && (frame->len != framedata->timer_interval)) {
			framedata->timer_interval = frame->len;
			ast_timer_set_rate(framedata->timer, 1000 / framedata->timer_interval);
		}
		if (!framedata->first) {
			framedata->first = 1;
			res = framedata->jb_impl->put_first(framedata->jb_obj, jbframe, now);
		} else {
			res = framedata->jb_impl->put(framedata->jb_obj, jbframe, now);
		}
		if (res == AST_JB_IMPL_OK) {
			frame = &ast_null_frame;
		}
		putframe = 1;
	}

	if (frame->frametype == AST_FRAME_NULL) {
		int res;
		long next = framedata->jb_impl->next(framedata->jb_obj);

		/* If now is earlier than the next expected output frame
		 * from the jitterbuffer we may choose to pass on retrieving
		 * a frame during this read iteration.  The only exception
		 * to this rule is when an audio frame is placed into the buffer
		 * and the time for the next frame to come out of the buffer is
		 * at least within the timer_interval of the next output frame. By
		 * doing this we are able to feed off the timing of the input frames
		 * and only rely on our jitterbuffer timer when frames are dropped.
		 * During testing, this hybrid form of timing gave more reliable results. */
		if (now < next) {
			long int diff = next - now;
			if (!putframe) {
				return frame;
			} else if (diff >= framedata->timer_interval) {
				return frame;
			}
		}

		res = framedata->jb_impl->get(framedata->jb_obj, &frame, now, framedata->timer_interval);
		switch (res) {
		case AST_JB_IMPL_OK:
			/* got it, and pass it through */
			break;
		case AST_JB_IMPL_DROP:
			ast_frfree(frame);
			frame = &ast_null_frame;
			break;
		case AST_JB_IMPL_INTERP:
			if (framedata->last_format.id) {
				struct ast_frame tmp = { 0, };
				tmp.frametype = AST_FRAME_VOICE;
				ast_format_copy(&tmp.subclass.format, &framedata->last_format);
				/* example: 8000hz / (1000 / 20ms) = 160 samples */
				tmp.samples = ast_format_rate(&framedata->last_format) / (1000 / framedata->timer_interval);
				tmp.delivery = ast_tvadd(framedata->start_tv, ast_samp2tv(next, 1000));
				tmp.offset = AST_FRIENDLY_OFFSET;
				tmp.src  = "func_jitterbuffer interpolation";
				frame = ast_frdup(&tmp);
				break;
			}
			/* else fall through */
		case AST_JB_IMPL_NOFRAME:
			frame = &ast_null_frame;
			break;
		}
	}

	return frame;
}

static int jb_helper(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct jb_framedata *framedata;
	struct ast_datastore *datastore = NULL;
	struct ast_framehook_interface interface = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = hook_event_cb,
		.destroy_cb = hook_destroy_cb,
	};
	int i = 0;

	if (!(framedata = ast_calloc(1, sizeof(*framedata)))) {
		return 0;
	}

	if (jb_framedata_init(framedata, data, value)) {
		jb_framedata_destroy(framedata);
		return 0;
	}

	interface.data = framedata;

	ast_channel_lock(chan);
	i = ast_framehook_attach(chan, &interface);
	if (i >= 0) {
		int *id;
		if ((datastore = ast_channel_datastore_find(chan, &jb_datastore, NULL))) {
			id = datastore->data;
			ast_framehook_detach(chan, *id);
			ast_channel_datastore_remove(chan, datastore);
		}

		if (!(datastore = ast_datastore_alloc(&jb_datastore, NULL))) {
			ast_framehook_detach(chan, i);
			ast_channel_unlock(chan);
			return 0;
		}

		if (!(id = ast_calloc(1, sizeof(int)))) {
			ast_datastore_free(datastore);
			ast_framehook_detach(chan, i);
			ast_channel_unlock(chan);
			return 0;
		}

		*id = i; /* Store off the id. The channel is still locked so it is safe to access this ptr. */
		datastore->data = id;
		ast_channel_datastore_add(chan, datastore);

		ast_channel_set_fd(chan, AST_JITTERBUFFER_FD, framedata->timer_fd);
	} else {
		jb_framedata_destroy(framedata);
		framedata = NULL;
	}
	ast_channel_unlock(chan);

	return 0;
}

static struct ast_custom_function jb_function = {
	.name = "JITTERBUFFER",
	.write = jb_helper,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&jb_function);
}

static int load_module(void)
{
	int res = ast_custom_function_register(&jb_function);
	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Jitter buffer for read side of channel.");

