/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Native RTP bridging technology module
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 * \ingroup bridges
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_technology.h"
#include "asterisk/frame.h"
#include "asterisk/rtp_engine.h"

/*! \brief Internal structure which contains information about bridged RTP channels */
struct native_rtp_bridge_data {
	/*! \brief Framehook used to intercept certain control frames */
	int id;
	/*! \brief Set when this framehook has been detached */
	unsigned int detached;
};

/*! \brief Internal helper function which gets all RTP information (glue and instances) relating to the given channels */
static enum ast_rtp_glue_result native_rtp_bridge_get(struct ast_channel *c0, struct ast_channel *c1, struct ast_rtp_glue **glue0,
	struct ast_rtp_glue **glue1, struct ast_rtp_instance **instance0, struct ast_rtp_instance **instance1,
	struct ast_rtp_instance **vinstance0, struct ast_rtp_instance **vinstance1)
{
	enum ast_rtp_glue_result audio_glue0_res;
	enum ast_rtp_glue_result video_glue0_res;
	enum ast_rtp_glue_result audio_glue1_res;
	enum ast_rtp_glue_result video_glue1_res;

	if (!(*glue0 = ast_rtp_instance_get_glue(ast_channel_tech(c0)->type)) ||
		!(*glue1 = ast_rtp_instance_get_glue(ast_channel_tech(c1)->type))) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	audio_glue0_res = (*glue0)->get_rtp_info(c0, instance0);
	video_glue0_res = (*glue0)->get_vrtp_info ? (*glue0)->get_vrtp_info(c0, vinstance0) : AST_RTP_GLUE_RESULT_FORBID;

	audio_glue1_res = (*glue1)->get_rtp_info(c1, instance1);
	video_glue1_res = (*glue1)->get_vrtp_info ? (*glue1)->get_vrtp_info(c1, vinstance1) : AST_RTP_GLUE_RESULT_FORBID;

	/* Apply any limitations on direct media bridging that may be present */
	if (audio_glue0_res == audio_glue1_res && audio_glue1_res == AST_RTP_GLUE_RESULT_REMOTE) {
		if ((*glue0)->allow_rtp_remote && !((*glue0)->allow_rtp_remote(c0, *instance1))) {
			/* If the allow_rtp_remote indicates that remote isn't allowed, revert to local bridge */
			audio_glue0_res = audio_glue1_res = AST_RTP_GLUE_RESULT_LOCAL;
		} else if ((*glue1)->allow_rtp_remote && !((*glue1)->allow_rtp_remote(c1, *instance0))) {
			audio_glue0_res = audio_glue1_res = AST_RTP_GLUE_RESULT_LOCAL;
		}
	}
	if (video_glue0_res == video_glue1_res && video_glue1_res == AST_RTP_GLUE_RESULT_REMOTE) {
		if ((*glue0)->allow_vrtp_remote && !((*glue0)->allow_vrtp_remote(c0, *instance1))) {
			/* if the allow_vrtp_remote indicates that remote isn't allowed, revert to local bridge */
			video_glue0_res = video_glue1_res = AST_RTP_GLUE_RESULT_LOCAL;
		} else if ((*glue1)->allow_vrtp_remote && !((*glue1)->allow_vrtp_remote(c1, *instance0))) {
			video_glue0_res = video_glue1_res = AST_RTP_GLUE_RESULT_LOCAL;
		}
	}

	/* If we are carrying video, and both sides are not going to remotely bridge... fail the native bridge */
	if (video_glue0_res != AST_RTP_GLUE_RESULT_FORBID
		&& (audio_glue0_res != AST_RTP_GLUE_RESULT_REMOTE
			|| video_glue0_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue0_res = AST_RTP_GLUE_RESULT_FORBID;
	}
	if (video_glue1_res != AST_RTP_GLUE_RESULT_FORBID
		&& (audio_glue1_res != AST_RTP_GLUE_RESULT_REMOTE
			|| video_glue1_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue1_res = AST_RTP_GLUE_RESULT_FORBID;
	}

	/* The order of preference is: forbid, local, and remote. */
	if (audio_glue0_res == AST_RTP_GLUE_RESULT_FORBID ||
		audio_glue1_res == AST_RTP_GLUE_RESULT_FORBID) {
		/* If any sort of bridge is forbidden just completely bail out and go back to generic bridging */
		return AST_RTP_GLUE_RESULT_FORBID;
	} else if (audio_glue0_res == AST_RTP_GLUE_RESULT_LOCAL ||
		audio_glue1_res == AST_RTP_GLUE_RESULT_LOCAL) {
		return AST_RTP_GLUE_RESULT_LOCAL;
	} else {
		return AST_RTP_GLUE_RESULT_REMOTE;
	}
}

/*!
 * \internal
 * \brief Start native RTP bridging of two channels
 *
 * \param bridge The bridge that had native RTP bridging happening on it
 * \param target If remote RTP bridging, the channel that is unheld.
 *
 * \note Bridge must be locked when calling this function.
 */
static void native_rtp_bridge_start(struct ast_bridge *bridge, struct ast_channel *target)
{
	struct ast_bridge_channel *bc0 = AST_LIST_FIRST(&bridge->channels);
	struct ast_bridge_channel *bc1 = AST_LIST_LAST(&bridge->channels);
	enum ast_rtp_glue_result native_type;
	struct ast_rtp_glue *glue0, *glue1;
	RAII_VAR(struct ast_rtp_instance *, instance0, NULL, ao2_cleanup);
	RAII_VAR(struct ast_rtp_instance *, instance1, NULL, ao2_cleanup);
	RAII_VAR(struct ast_rtp_instance *, vinstance0, NULL, ao2_cleanup);
	RAII_VAR(struct ast_rtp_instance *, vinstance1, NULL, ao2_cleanup);
	RAII_VAR(struct ast_rtp_instance *, tinstance0, NULL, ao2_cleanup);
	RAII_VAR(struct ast_rtp_instance *, tinstance1, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, cap0, ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT), ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, cap1, ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT), ao2_cleanup);

	if (bc0 == bc1) {
		return;
	}

	ast_channel_lock_both(bc0->chan, bc1->chan);
	native_type = native_rtp_bridge_get(bc0->chan, bc1->chan, &glue0, &glue1, &instance0, &instance1, &vinstance0, &vinstance1);

	switch (native_type) {
	case AST_RTP_GLUE_RESULT_LOCAL:
		if (ast_rtp_instance_get_engine(instance0)->local_bridge) {
			ast_rtp_instance_get_engine(instance0)->local_bridge(instance0, instance1);
		}
		if (ast_rtp_instance_get_engine(instance1)->local_bridge) {
			ast_rtp_instance_get_engine(instance1)->local_bridge(instance1, instance0);
		}
		ast_rtp_instance_set_bridged(instance0, instance1);
		ast_rtp_instance_set_bridged(instance1, instance0);
		ast_debug(2, "Locally RTP bridged '%s' and '%s' in stack\n",
			ast_channel_name(bc0->chan), ast_channel_name(bc1->chan));
		break;

	case AST_RTP_GLUE_RESULT_REMOTE:
		if (glue0->get_codec) {
			glue0->get_codec(bc0->chan, cap0);
		}
		if (glue1->get_codec) {
			glue1->get_codec(bc1->chan, cap1);
		}

		/* If we have a target, it's the channel that received the UNHOLD or UPDATE_RTP_PEER frame and was told to resume */
		if (!target) {
			glue0->update_peer(bc0->chan, instance1, vinstance1, tinstance1, cap1, 0);
			glue1->update_peer(bc1->chan, instance0, vinstance0, tinstance0, cap0, 0);
			ast_debug(2, "Remotely bridged '%s' and '%s' - media will flow directly between them\n",
				ast_channel_name(bc0->chan), ast_channel_name(bc1->chan));
		} else {
			/*
			 * If a target was provided, it is the recipient of an unhold or an update and needs to have
			 * its media redirected to fit the current remote bridging needs. The other channel is either
			 * already set up to handle the new media path or will have its own set of updates independent
			 * of this pass.
			 */
			if (bc0->chan == target) {
				glue0->update_peer(bc0->chan, instance1, vinstance1, tinstance1, cap1, 0);
			} else {
				glue1->update_peer(bc1->chan, instance0, vinstance0, tinstance0, cap0, 0);
			}
		}
		break;
	case AST_RTP_GLUE_RESULT_FORBID:
		break;
	}

	ast_channel_unlock(bc0->chan);
	ast_channel_unlock(bc1->chan);
}

static void native_rtp_bridge_stop(struct ast_bridge *bridge, struct ast_channel *target)
{
	struct ast_bridge_channel *bc0 = AST_LIST_FIRST(&bridge->channels);
	struct ast_bridge_channel *bc1 = AST_LIST_LAST(&bridge->channels);
	enum ast_rtp_glue_result native_type;
	struct ast_rtp_glue *glue0, *glue1 = NULL;
	RAII_VAR(struct ast_rtp_instance *, instance0, NULL, ao2_cleanup);
	RAII_VAR(struct ast_rtp_instance *, instance1, NULL, ao2_cleanup);
	RAII_VAR(struct ast_rtp_instance *, vinstance0, NULL, ao2_cleanup);
	RAII_VAR(struct ast_rtp_instance *, vinstance1, NULL, ao2_cleanup);

	if (bc0 == bc1) {
		return;
	}

	ast_channel_lock_both(bc0->chan, bc1->chan);
	native_type = native_rtp_bridge_get(bc0->chan, bc1->chan, &glue0, &glue1, &instance0, &instance1, &vinstance0, &vinstance1);

	switch (native_type) {
	case AST_RTP_GLUE_RESULT_LOCAL:
		if (ast_rtp_instance_get_engine(instance0)->local_bridge) {
			ast_rtp_instance_get_engine(instance0)->local_bridge(instance0, NULL);
		}
		if (instance1 && ast_rtp_instance_get_engine(instance1)->local_bridge) {
			ast_rtp_instance_get_engine(instance1)->local_bridge(instance1, NULL);
		}
		ast_rtp_instance_set_bridged(instance0, NULL);
		if (instance1) {
			ast_rtp_instance_set_bridged(instance1, NULL);
		}
		break;
	case AST_RTP_GLUE_RESULT_REMOTE:
		if (target) {
			/*
			 * If a target was provided, it is being put on hold and should expect to
			 * receive media from Asterisk instead of what it was previously connected to.
			 */
			if (bc0->chan == target) {
				glue0->update_peer(bc0->chan, NULL, NULL, NULL, NULL, 0);
			} else {
				glue1->update_peer(bc1->chan, NULL, NULL, NULL, NULL, 0);
			}
		}
		break;
	case AST_RTP_GLUE_RESULT_FORBID:
		break;
	}

	if (!target && native_type != AST_RTP_GLUE_RESULT_FORBID) {
		glue0->update_peer(bc0->chan, NULL, NULL, NULL, NULL, 0);
		glue1->update_peer(bc1->chan, NULL, NULL, NULL, NULL, 0);
	}

	ast_debug(2, "Discontinued RTP bridging of '%s' and '%s' - media will flow through Asterisk core\n",
		ast_channel_name(bc0->chan), ast_channel_name(bc1->chan));

	ast_channel_unlock(bc0->chan);
	ast_channel_unlock(bc1->chan);
}

/*! \brief Frame hook that is called to intercept hold/unhold */
static struct ast_frame *native_rtp_framehook(struct ast_channel *chan, struct ast_frame *f, enum ast_framehook_event event, void *data)
{
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	struct native_rtp_bridge_data *native_data = data;

	if (!f || (event != AST_FRAMEHOOK_EVENT_WRITE)) {
		return f;
	}

	bridge = ast_channel_get_bridge(chan);

	if (bridge) {
		/* native_rtp_bridge_start/stop are not being called from bridging
		   core so we need to lock the bridge prior to calling these functions
		   Unfortunately that means unlocking the channel, but as it
		   should not be modified this should be okay... hopefully...
		   unless this channel is being moved around right now and is in
		   the process of having this framehook removed (which is fine). To
		   ensure we then don't stop or start when we shouldn't we consult
		   the data provided. If this framehook has been detached then the
		   detached variable will be set. This is safe to check as it is only
		   manipulated with the bridge lock held. */
		ast_channel_unlock(chan);
		ast_bridge_lock(bridge);
		if (!native_data->detached) {
			if (f->subclass.integer == AST_CONTROL_HOLD) {
				native_rtp_bridge_stop(bridge, chan);
			} else if ((f->subclass.integer == AST_CONTROL_UNHOLD) ||
				(f->subclass.integer == AST_CONTROL_UPDATE_RTP_PEER)) {
				native_rtp_bridge_start(bridge, chan);
			}
		}
		ast_bridge_unlock(bridge);
		ast_channel_lock(chan);

	}

	return f;
}

/*! \brief Callback function which informs upstream if we are consuming a frame of a specific type */
static int native_rtp_framehook_consume(void *data, enum ast_frame_type type)
{
	return (type == AST_FRAME_CONTROL ? 1 : 0);
}

/*! \brief Internal helper function which checks whether the channels are compatible with our native bridging */
static int native_rtp_bridge_capable(struct ast_channel *chan)
{
	return !ast_channel_has_hook_requiring_audio(chan);
}

static int native_rtp_bridge_compatible(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *bc0 = AST_LIST_FIRST(&bridge->channels);
	struct ast_bridge_channel *bc1 = AST_LIST_LAST(&bridge->channels);
	enum ast_rtp_glue_result native_type;
	struct ast_rtp_glue *glue0, *glue1;
	RAII_VAR(struct ast_rtp_instance *, instance0, NULL, ao2_cleanup);
	RAII_VAR(struct ast_rtp_instance *, instance1, NULL, ao2_cleanup);
	RAII_VAR(struct ast_rtp_instance *, vinstance0, NULL, ao2_cleanup);
	RAII_VAR(struct ast_rtp_instance *, vinstance1, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, cap0, ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT), ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, cap1, ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT), ao2_cleanup);
	int read_ptime0, read_ptime1, write_ptime0, write_ptime1;

	/* We require two channels before even considering native bridging */
	if (bridge->num_channels != 2) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as two channels are required\n",
			bridge->uniqueid);
		return 0;
	}

	if (!native_rtp_bridge_capable(bc0->chan)) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as channel '%s' has features which prevent it\n",
			bridge->uniqueid, ast_channel_name(bc0->chan));
		return 0;
	}

	if (!native_rtp_bridge_capable(bc1->chan)) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as channel '%s' has features which prevent it\n",
			bridge->uniqueid, ast_channel_name(bc1->chan));
		return 0;
	}

	if ((native_type = native_rtp_bridge_get(bc0->chan, bc1->chan, &glue0, &glue1, &instance0, &instance1, &vinstance0, &vinstance1))
		== AST_RTP_GLUE_RESULT_FORBID) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as it was forbidden while getting details\n",
			bridge->uniqueid);
		return 0;
	}

	if (ao2_container_count(bc0->features->dtmf_hooks) && ast_rtp_instance_dtmf_mode_get(instance0)) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as channel '%s' has DTMF hooks\n",
			bridge->uniqueid, ast_channel_name(bc0->chan));
		return 0;
	}

	if (ao2_container_count(bc1->features->dtmf_hooks) && ast_rtp_instance_dtmf_mode_get(instance1)) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as channel '%s' has DTMF hooks\n",
			bridge->uniqueid, ast_channel_name(bc1->chan));
		return 0;
	}

	if ((native_type == AST_RTP_GLUE_RESULT_LOCAL) && ((ast_rtp_instance_get_engine(instance0)->local_bridge !=
		ast_rtp_instance_get_engine(instance1)->local_bridge) ||
		(ast_rtp_instance_get_engine(instance0)->dtmf_compatible &&
			!ast_rtp_instance_get_engine(instance0)->dtmf_compatible(bc0->chan, instance0, bc1->chan, instance1)))) {
		ast_debug(1, "Bridge '%s' can not use local native RTP bridge as local bridge or DTMF is not compatible\n",
			bridge->uniqueid);
		return 0;
	}

	/* Make sure that codecs match */
	if (glue0->get_codec) {
		glue0->get_codec(bc0->chan, cap0);
	}
	if (glue1->get_codec) {
		glue1->get_codec(bc1->chan, cap1);
	}
	if (ast_format_cap_count(cap0) != 0 && ast_format_cap_count(cap1) != 0 && !ast_format_cap_iscompatible(cap0, cap1)) {
		struct ast_str *codec_buf0 = ast_str_alloca(64);
		struct ast_str *codec_buf1 = ast_str_alloca(64);
		ast_debug(1, "Channel codec0 = %s is not codec1 = %s, cannot native bridge in RTP.\n",
			ast_format_cap_get_names(cap0, &codec_buf0), ast_format_cap_get_names(cap1, &codec_buf1));
		return 0;
	}

	read_ptime0 = ast_format_cap_get_format_framing(cap0, ast_channel_rawreadformat(bc0->chan));
	read_ptime1 = ast_format_cap_get_format_framing(cap1, ast_channel_rawreadformat(bc1->chan));
	write_ptime0 = ast_format_cap_get_format_framing(cap0, ast_channel_rawwriteformat(bc0->chan));
	write_ptime1 = ast_format_cap_get_format_framing(cap1, ast_channel_rawwriteformat(bc1->chan));

	if (read_ptime0 != write_ptime1 || read_ptime1 != write_ptime0) {
		ast_debug(1, "Packetization differs between RTP streams (%d != %d or %d != %d). Cannot native bridge in RTP\n",
				read_ptime0, write_ptime1, read_ptime1, write_ptime0);
		return 0;
	}

	return 1;
}

/*! \brief Helper function which adds frame hook to bridge channel */
static int native_rtp_bridge_framehook_attach(struct ast_bridge_channel *bridge_channel)
{
	struct native_rtp_bridge_data *data = ao2_alloc(sizeof(*data), NULL);
	static struct ast_framehook_interface hook = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = native_rtp_framehook,
		.destroy_cb = __ao2_cleanup,
		.consume_cb = native_rtp_framehook_consume,
		.disable_inheritance = 1,
	};

	if (!data) {
		return -1;
	}

	ast_channel_lock(bridge_channel->chan);
	hook.data = ao2_bump(data);
	data->id = ast_framehook_attach(bridge_channel->chan, &hook);
	ast_channel_unlock(bridge_channel->chan);
	if (data->id < 0) {
		/* We need to drop both the reference we hold, and the one the framehook would hold */
		ao2_ref(data, -2);
		return -1;
	}

	bridge_channel->tech_pvt = data;

	return 0;
}

/*! \brief Helper function which removes frame hook from bridge channel */
static void native_rtp_bridge_framehook_detach(struct ast_bridge_channel *bridge_channel)
{
	RAII_VAR(struct native_rtp_bridge_data *, data, bridge_channel->tech_pvt, ao2_cleanup);

	if (!data) {
		return;
	}

	ast_channel_lock(bridge_channel->chan);
	ast_framehook_detach(bridge_channel->chan, data->id);
	data->detached = 1;
	ast_channel_unlock(bridge_channel->chan);
	bridge_channel->tech_pvt = NULL;
}

static int native_rtp_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	native_rtp_bridge_framehook_detach(bridge_channel);
	if (native_rtp_bridge_framehook_attach(bridge_channel)) {
		return -1;
	}

	native_rtp_bridge_start(bridge, NULL);
	return 0;
}

static void native_rtp_bridge_unsuspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	native_rtp_bridge_join(bridge, bridge_channel);
}

static void native_rtp_bridge_leave(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	native_rtp_bridge_framehook_detach(bridge_channel);
	native_rtp_bridge_stop(bridge, NULL);
}

static int native_rtp_bridge_write(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	return ast_bridge_queue_everyone_else(bridge, bridge_channel, frame);
}

static struct ast_bridge_technology native_rtp_bridge = {
	.name = "native_rtp",
	.capabilities = AST_BRIDGE_CAPABILITY_NATIVE,
	.preference = AST_BRIDGE_PREFERENCE_BASE_NATIVE,
	.join = native_rtp_bridge_join,
	.unsuspend = native_rtp_bridge_unsuspend,
	.leave = native_rtp_bridge_leave,
	.suspend = native_rtp_bridge_leave,
	.write = native_rtp_bridge_write,
	.compatible = native_rtp_bridge_compatible,
};

static int unload_module(void)
{
	ao2_t_ref(native_rtp_bridge.format_capabilities, -1, "Dispose of capabilities in module unload");
	return ast_bridge_technology_unregister(&native_rtp_bridge);
}

static int load_module(void)
{
	if (!(native_rtp_bridge.format_capabilities = ast_format_cap_alloc(0))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append_by_type(native_rtp_bridge.format_capabilities, AST_MEDIA_TYPE_AUDIO);
	ast_format_cap_append_by_type(native_rtp_bridge.format_capabilities, AST_MEDIA_TYPE_VIDEO);
	ast_format_cap_append_by_type(native_rtp_bridge.format_capabilities, AST_MEDIA_TYPE_TEXT);

	return ast_bridge_technology_register(&native_rtp_bridge);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Native RTP bridging module");
