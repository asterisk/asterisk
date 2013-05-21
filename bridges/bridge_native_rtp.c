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
 * \brief Native RTP bridging module
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
#include "asterisk/bridging.h"
#include "asterisk/bridging_technology.h"
#include "asterisk/frame.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/audiohook.h"

/*! \brief Forward declarations for frame hook usage */
static int native_rtp_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel);
static void native_rtp_bridge_leave(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel);

/*! \brief Internal structure which contains information about bridged RTP channels */
struct native_rtp_bridge_data {
	/*! \brief Framehook used to intercept certain control frames */
	int id;
};

/*! \brief Frame hook that is called to intercept hold/unhold */
static struct ast_frame *native_rtp_framehook(struct ast_channel *chan, struct ast_frame *f, enum ast_framehook_event event, void *data)
{
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);

	if (!f || (event != AST_FRAMEHOOK_EVENT_WRITE)) {
		return f;
	}

	ast_channel_lock(chan);
	bridge = ast_channel_get_bridge(chan);
	ast_channel_unlock(chan);

	/* It's safe for NULL to be passed to both of these, bridge_channel isn't used at all */
	if (bridge) {
		if (f->subclass.integer == AST_CONTROL_HOLD) {
			native_rtp_bridge_leave(ast_channel_internal_bridge(chan), NULL);
		} else if ((f->subclass.integer == AST_CONTROL_UNHOLD) || (f->subclass.integer == AST_CONTROL_UPDATE_RTP_PEER)) {
			native_rtp_bridge_join(ast_channel_internal_bridge(chan), NULL);
		}
	}

	return f;
}

/*! \brief Internal helper function which checks whether the channels are compatible with our native bridging */
static int native_rtp_bridge_capable(struct ast_channel *chan)
{
	if (ast_channel_monitor(chan) || (ast_channel_audiohooks(chan) &&
		!ast_audiohook_write_list_empty(ast_channel_audiohooks(chan))) ||
		!ast_framehook_list_is_empty(ast_channel_framehooks(chan))) {
		return 0;
	} else {
		return 1;
	}
}

/*! \brief Internal helper function which gets all RTP information (glue and instances) relating to the given channels */
static enum ast_rtp_glue_result native_rtp_bridge_get(struct ast_channel *c0, struct ast_channel *c1, struct ast_rtp_glue **glue0,
	struct ast_rtp_glue **glue1, struct ast_rtp_instance **instance0, struct ast_rtp_instance **instance1,
	struct ast_rtp_instance **vinstance0, struct ast_rtp_instance **vinstance1)
{
	enum ast_rtp_glue_result audio_glue0_res = AST_RTP_GLUE_RESULT_FORBID, video_glue0_res = AST_RTP_GLUE_RESULT_FORBID;
	enum ast_rtp_glue_result audio_glue1_res = AST_RTP_GLUE_RESULT_FORBID, video_glue1_res = AST_RTP_GLUE_RESULT_FORBID;

	if (!(*glue0 = ast_rtp_instance_get_glue(ast_channel_tech(c0)->type)) ||
		(c1 && !(*glue1 = ast_rtp_instance_get_glue(ast_channel_tech(c1)->type)))) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	audio_glue0_res = (*glue0)->get_rtp_info(c0, instance0);
	video_glue0_res = (*glue0)->get_vrtp_info ? (*glue0)->get_vrtp_info(c0, vinstance0) : AST_RTP_GLUE_RESULT_FORBID;

	if (c1) {
		audio_glue1_res = (*glue1)->get_rtp_info(c1, instance1);
		video_glue1_res = (*glue1)->get_vrtp_info ? (*glue1)->get_vrtp_info(c1, vinstance1) : AST_RTP_GLUE_RESULT_FORBID;
	}

	/* Apply any limitations on direct media bridging that may be present */
	if (audio_glue0_res == audio_glue1_res && audio_glue1_res == AST_RTP_GLUE_RESULT_REMOTE) {
		if ((*glue0)->allow_rtp_remote && !((*glue0)->allow_rtp_remote(c0, *instance1))) {
			/* If the allow_rtp_remote indicates that remote isn't allowed, revert to local bridge */
			audio_glue0_res = audio_glue1_res = AST_RTP_GLUE_RESULT_LOCAL;
		} else if ((*glue1)->allow_rtp_remote && !((*glue1)->allow_rtp_remote(c1, *instance0))) {
			audio_glue0_res = audio_glue1_res = AST_RTP_GLUE_RESULT_LOCAL;
		}
	}
	if (c1 && video_glue0_res == video_glue1_res && video_glue1_res == AST_RTP_GLUE_RESULT_REMOTE) {
		if ((*glue0)->allow_vrtp_remote && !((*glue0)->allow_vrtp_remote(c0, *instance1))) {
			/* if the allow_vrtp_remote indicates that remote isn't allowed, revert to local bridge */
			video_glue0_res = video_glue1_res = AST_RTP_GLUE_RESULT_LOCAL;
		} else if ((*glue1)->allow_vrtp_remote && !((*glue1)->allow_vrtp_remote(c1, *instance0))) {
			video_glue0_res = video_glue1_res = AST_RTP_GLUE_RESULT_LOCAL;
		}
	}

	/* If we are carrying video, and both sides are not going to remotely bridge... fail the native bridge */
	if (video_glue0_res != AST_RTP_GLUE_RESULT_FORBID && (audio_glue0_res != AST_RTP_GLUE_RESULT_REMOTE || video_glue0_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue0_res = AST_RTP_GLUE_RESULT_FORBID;
	}
	if (c1 && video_glue1_res != AST_RTP_GLUE_RESULT_FORBID && (audio_glue1_res != AST_RTP_GLUE_RESULT_REMOTE || video_glue1_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue1_res = AST_RTP_GLUE_RESULT_FORBID;
	}

	/* If any sort of bridge is forbidden just completely bail out and go back to generic bridging */
	if (audio_glue0_res == AST_RTP_GLUE_RESULT_FORBID || (c1 && audio_glue1_res == AST_RTP_GLUE_RESULT_FORBID)) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	return audio_glue0_res;
}

static int native_rtp_bridge_compatible(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *c0 = AST_LIST_FIRST(&bridge->channels);
	struct ast_bridge_channel *c1 = AST_LIST_LAST(&bridge->channels);
	enum ast_rtp_glue_result native_type;
	struct ast_rtp_glue *glue0, *glue1;
	struct ast_rtp_instance *instance0 = NULL, *instance1 = NULL, *vinstance0 = NULL, *vinstance1 = NULL;
	RAII_VAR(struct ast_format_cap *, cap0, ast_format_cap_alloc_nolock(), ast_format_cap_destroy);
	RAII_VAR(struct ast_format_cap *, cap1, ast_format_cap_alloc_nolock(), ast_format_cap_destroy);
	int read_ptime0, read_ptime1, write_ptime0, write_ptime1;

	/* We require two channels before even considering native bridging */
	if (bridge->num_channels != 2) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as two channels are required\n",
			bridge->uniqueid);
		return 0;
	}

	if (!native_rtp_bridge_capable(c0->chan)) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as channel '%s' has features which prevent it\n",
			bridge->uniqueid, ast_channel_name(c0->chan));
		return 0;
	}

	if (!native_rtp_bridge_capable(c1->chan)) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as channel '%s' has features which prevent it\n",
			bridge->uniqueid, ast_channel_name(c1->chan));
		return 0;
	}

	if ((native_type = native_rtp_bridge_get(c0->chan, c1->chan, &glue0, &glue1, &instance0, &instance1, &vinstance0, &vinstance1))
		== AST_RTP_GLUE_RESULT_FORBID) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as it was forbidden while getting details\n",
			bridge->uniqueid);
		return 0;
	}

	if (ao2_container_count(c0->features->dtmf_hooks) && ast_rtp_instance_dtmf_mode_get(instance0)) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as channel '%s' has DTMF hooks\n",
			bridge->uniqueid, ast_channel_name(c0->chan));
		return 0;
	}

	if (ao2_container_count(c1->features->dtmf_hooks) && ast_rtp_instance_dtmf_mode_get(instance1)) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as channel '%s' has DTMF hooks\n",
			bridge->uniqueid, ast_channel_name(c1->chan));
		return 0;
	}

	if ((native_type == AST_RTP_GLUE_RESULT_LOCAL) && ((ast_rtp_instance_get_engine(instance0)->local_bridge !=
		ast_rtp_instance_get_engine(instance1)->local_bridge) ||
		(ast_rtp_instance_get_engine(instance0)->dtmf_compatible &&
			!ast_rtp_instance_get_engine(instance0)->dtmf_compatible(c0->chan, instance0, c1->chan, instance1)))) {
		ast_debug(1, "Bridge '%s' can not use local native RTP bridge as local bridge or DTMF is not compatible\n",
			bridge->uniqueid);
		return 0;
	}

	/* Make sure that codecs match */
	if (glue0->get_codec) {
		glue0->get_codec(c0->chan, cap0);
	}
	if (glue1->get_codec) {
		glue1->get_codec(c1->chan, cap1);
	}
	if (!ast_format_cap_is_empty(cap0) && !ast_format_cap_is_empty(cap1) && !ast_format_cap_has_joint(cap0, cap1)) {
		char tmp0[256] = { 0, }, tmp1[256] = { 0, };

		ast_debug(1, "Channel codec0 = %s is not codec1 = %s, cannot native bridge in RTP.\n",
			ast_getformatname_multiple(tmp0, sizeof(tmp0), cap0),
			ast_getformatname_multiple(tmp1, sizeof(tmp1), cap1));
		return 0;
	}

	read_ptime0 = (ast_codec_pref_getsize(&ast_rtp_instance_get_codecs(instance0)->pref, ast_channel_rawreadformat(c0->chan))).cur_ms;
	read_ptime1 = (ast_codec_pref_getsize(&ast_rtp_instance_get_codecs(instance1)->pref, ast_channel_rawreadformat(c1->chan))).cur_ms;
	write_ptime0 = (ast_codec_pref_getsize(&ast_rtp_instance_get_codecs(instance0)->pref, ast_channel_rawwriteformat(c0->chan))).cur_ms;
	write_ptime1 = (ast_codec_pref_getsize(&ast_rtp_instance_get_codecs(instance1)->pref, ast_channel_rawwriteformat(c1->chan))).cur_ms;

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
	};

	if (!data) {
		return -1;
	}

	ast_channel_lock(bridge_channel->chan);

	if (!(data->id = ast_framehook_attach(bridge_channel->chan, &hook)) < 0) {
		ast_channel_unlock(bridge_channel->chan);
		ao2_cleanup(data);
		return -1;
	}

	ast_channel_unlock(bridge_channel->chan);

	bridge_channel->bridge_pvt = data;

	return 0;
}

/*! \brief Helper function which removes frame hook from bridge channel */
static void native_rtp_bridge_framehook_detach(struct ast_bridge_channel *bridge_channel)
{
	RAII_VAR(struct native_rtp_bridge_data *, data, bridge_channel->bridge_pvt, ao2_cleanup);

	if (!data) {
		return;
	}

	ast_channel_lock(bridge_channel->chan);
	ast_framehook_detach(bridge_channel->chan, data->id);
	ast_channel_unlock(bridge_channel->chan);
	bridge_channel->bridge_pvt = NULL;
}

static int native_rtp_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_channel *c0 = AST_LIST_FIRST(&bridge->channels);
	struct ast_bridge_channel *c1 = AST_LIST_LAST(&bridge->channels);
	enum ast_rtp_glue_result native_type;
	struct ast_rtp_glue *glue0, *glue1;
	struct ast_rtp_instance *instance0 = NULL, *instance1 = NULL, *vinstance0 = NULL;
	struct ast_rtp_instance *vinstance1 = NULL, *tinstance0 = NULL, *tinstance1 = NULL;
	RAII_VAR(struct ast_format_cap *, cap0, ast_format_cap_alloc_nolock(), ast_format_cap_destroy);
	RAII_VAR(struct ast_format_cap *, cap1, ast_format_cap_alloc_nolock(), ast_format_cap_destroy);

	native_rtp_bridge_framehook_detach(c0);
	if (native_rtp_bridge_framehook_attach(c0)) {
		return -1;
	}

	native_rtp_bridge_framehook_detach(c1);
	if (native_rtp_bridge_framehook_attach(c1)) {
		native_rtp_bridge_framehook_detach(c0);
		return -1;
	}

	native_type = native_rtp_bridge_get(c0->chan, c1->chan, &glue0, &glue1, &instance0, &instance1, &vinstance0, &vinstance1);

	if (glue0->get_codec) {
		glue0->get_codec(c0->chan, cap0);
	}
	if (glue1->get_codec) {
		glue1->get_codec(c1->chan, cap1);
	}

	if (native_type == AST_RTP_GLUE_RESULT_LOCAL) {
		if (ast_rtp_instance_get_engine(instance0)->local_bridge) {
			ast_rtp_instance_get_engine(instance0)->local_bridge(instance0, instance1);
		}
		if (ast_rtp_instance_get_engine(instance1)->local_bridge) {
			ast_rtp_instance_get_engine(instance1)->local_bridge(instance1, instance0);
		}
		ast_rtp_instance_set_bridged(instance0, instance1);
		ast_rtp_instance_set_bridged(instance1, instance0);
	} else {
		glue0->update_peer(c0->chan, instance1, vinstance1, tinstance1, cap1, 0);
		glue1->update_peer(c1->chan, instance0, vinstance0, tinstance0, cap0, 0);
	}

	return 0;
}

static void native_rtp_bridge_unsuspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	native_rtp_bridge_join(bridge, bridge_channel);
}

static void native_rtp_bridge_leave(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_channel *c0 = AST_LIST_FIRST(&bridge->channels) ? AST_LIST_FIRST(&bridge->channels) : bridge_channel;
	struct ast_bridge_channel *c1 = AST_LIST_LAST(&bridge->channels);
	enum ast_rtp_glue_result native_type;
	struct ast_rtp_glue *glue0, *glue1 = NULL;
	struct ast_rtp_instance *instance0 = NULL, *instance1 = NULL, *vinstance0 = NULL, *vinstance1 = NULL;

	native_rtp_bridge_framehook_detach(c0);
	if (c1) {
		native_rtp_bridge_framehook_detach(c1);
	}

	native_type = native_rtp_bridge_get(c0->chan, c1 ? c1->chan : NULL, &glue0, &glue1, &instance0, &instance1, &vinstance0, &vinstance1);

	if (native_type == AST_RTP_GLUE_RESULT_LOCAL) {
		if (ast_rtp_instance_get_engine(instance0)->local_bridge) {
			ast_rtp_instance_get_engine(instance0)->local_bridge(instance0, NULL);
		}
		if (instance1 && ast_rtp_instance_get_engine(instance1)->local_bridge) {
			ast_rtp_instance_get_engine(instance1)->local_bridge(instance1, NULL);
		}
		ast_rtp_instance_set_bridged(instance0, instance1);
		if (instance1) {
			ast_rtp_instance_set_bridged(instance1, instance0);
		}
	} else {
		glue0->update_peer(c0->chan, NULL, NULL, NULL, NULL, 0);
		if (glue1) {
			glue1->update_peer(c1->chan, NULL, NULL, NULL, NULL, 0);
		}
	}
}

static int native_rtp_bridge_write(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct ast_bridge_channel *other = ast_bridge_channel_peer(bridge_channel);

	if (!other) {
		return -1;
	}

	/* The bridging core takes care of freeing the passed in frame. */
	ast_bridge_channel_queue_frame(other, frame);

	return 0;
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
	ast_format_cap_destroy(native_rtp_bridge.format_capabilities);
	return ast_bridge_technology_unregister(&native_rtp_bridge);
}

static int load_module(void)
{
	if (!(native_rtp_bridge.format_capabilities = ast_format_cap_alloc())) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_add_all_by_type(native_rtp_bridge.format_capabilities, AST_FORMAT_TYPE_AUDIO);
	ast_format_cap_add_all_by_type(native_rtp_bridge.format_capabilities, AST_FORMAT_TYPE_VIDEO);
	ast_format_cap_add_all_by_type(native_rtp_bridge.format_capabilities, AST_FORMAT_TYPE_TEXT);

	return ast_bridge_technology_register(&native_rtp_bridge);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Native RTP bridging module");
