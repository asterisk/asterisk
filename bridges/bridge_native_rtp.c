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

/*! \brief Internal structure which contains bridged RTP channel hook data */
struct native_rtp_framehook_data {
	/*! \brief Framehook used to intercept certain control frames */
	int id;
	/*! \brief Set when this framehook has been detached */
	unsigned int detached;
};

struct rtp_glue_stream {
	/*! \brief RTP instance */
	struct ast_rtp_instance *instance;
	/*! \brief glue result */
	enum ast_rtp_glue_result result;
};

struct rtp_glue_data {
	/*!
	 * \brief glue callbacks
	 *
	 * \note The glue data is considered valid if cb is not NULL.
	 */
	struct ast_rtp_glue *cb;
	struct rtp_glue_stream audio;
	struct rtp_glue_stream video;
	/*! Combined glue result of both bridge channels. */
	enum ast_rtp_glue_result result;
};

/*! \brief Internal structure which contains instance information about bridged RTP channels */
struct native_rtp_bridge_channel_data {
	/*! \brief Channel's hook data */
	struct native_rtp_framehook_data *hook_data;
	/*!
	 * \brief Glue callbacks to bring remote channel streams back to Asterisk.
	 * \note NULL if channel streams are local.
	 */
	struct ast_rtp_glue *remote_cb;
	/*! \brief Channel's cached RTP glue information */
	struct rtp_glue_data glue;
};

static void rtp_glue_data_init(struct rtp_glue_data *glue)
{
	glue->cb = NULL;
	glue->audio.instance = NULL;
	glue->audio.result = AST_RTP_GLUE_RESULT_FORBID;
	glue->video.instance = NULL;
	glue->video.result = AST_RTP_GLUE_RESULT_FORBID;
	glue->result = AST_RTP_GLUE_RESULT_FORBID;
}

static void rtp_glue_data_destroy(struct rtp_glue_data *glue)
{
	if (!glue) {
		return;
	}
	ao2_cleanup(glue->audio.instance);
	ao2_cleanup(glue->video.instance);
}

static void rtp_glue_data_reset(struct rtp_glue_data *glue)
{
	rtp_glue_data_destroy(glue);
	rtp_glue_data_init(glue);
}

static void native_rtp_bridge_channel_data_free(struct native_rtp_bridge_channel_data *data)
{
	ast_debug(2, "Destroying channel tech_pvt data %p\n", data);

	/*
	 * hook_data will probably already have been unreferenced by the framehook detach
	 * and the pointer set to null.
	 */
	ao2_cleanup(data->hook_data);

	rtp_glue_data_reset(&data->glue);
	ast_free(data);
}

static struct native_rtp_bridge_channel_data *native_rtp_bridge_channel_data_alloc(void)
{
	struct native_rtp_bridge_channel_data *data;

	data = ast_calloc(1, sizeof(*data));
	if (data) {
		rtp_glue_data_init(&data->glue);
	}
	return data;
}

/*!
 * \internal
 * \brief Helper function which gets all RTP information (glue and instances) relating to the given channels
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int rtp_glue_data_get(struct ast_channel *c0, struct rtp_glue_data *glue0,
	struct ast_channel *c1, struct rtp_glue_data *glue1)
{
	struct ast_rtp_glue *cb0;
	struct ast_rtp_glue *cb1;
	enum ast_rtp_glue_result combined_result;

	cb0 = ast_rtp_instance_get_glue(ast_channel_tech(c0)->type);
	cb1 = ast_rtp_instance_get_glue(ast_channel_tech(c1)->type);
	if (!cb0 || !cb1) {
		/* One or both channels doesn't have any RTP glue registered. */
		return -1;
	}

	/* The glue callbacks bump the RTP instance refcounts for us. */

	glue0->cb = cb0;
	glue0->audio.result = cb0->get_rtp_info(c0, &glue0->audio.instance);
	glue0->video.result = cb0->get_vrtp_info
		? cb0->get_vrtp_info(c0, &glue0->video.instance) : AST_RTP_GLUE_RESULT_FORBID;

	glue1->cb = cb1;
	glue1->audio.result = cb1->get_rtp_info(c1, &glue1->audio.instance);
	glue1->video.result = cb1->get_vrtp_info
		? cb1->get_vrtp_info(c1, &glue1->video.instance) : AST_RTP_GLUE_RESULT_FORBID;

	/*
	 * Now determine the combined glue result.
	 */

	/* Apply any limitations on direct media bridging that may be present */
	if (glue0->audio.result == glue1->audio.result && glue1->audio.result == AST_RTP_GLUE_RESULT_REMOTE) {
		if (glue0->cb->allow_rtp_remote && !glue0->cb->allow_rtp_remote(c0, glue1->audio.instance)) {
			/* If the allow_rtp_remote indicates that remote isn't allowed, revert to local bridge */
			glue0->audio.result = glue1->audio.result = AST_RTP_GLUE_RESULT_LOCAL;
		} else if (glue1->cb->allow_rtp_remote && !glue1->cb->allow_rtp_remote(c1, glue0->audio.instance)) {
			glue0->audio.result = glue1->audio.result = AST_RTP_GLUE_RESULT_LOCAL;
		}
	}
	if (glue0->video.result == glue1->video.result && glue1->video.result == AST_RTP_GLUE_RESULT_REMOTE) {
		if (glue0->cb->allow_vrtp_remote && !glue0->cb->allow_vrtp_remote(c0, glue1->video.instance)) {
			/* If the allow_vrtp_remote indicates that remote isn't allowed, revert to local bridge */
			glue0->video.result = glue1->video.result = AST_RTP_GLUE_RESULT_LOCAL;
		} else if (glue1->cb->allow_vrtp_remote && !glue1->cb->allow_vrtp_remote(c1, glue0->video.instance)) {
			glue0->video.result = glue1->video.result = AST_RTP_GLUE_RESULT_LOCAL;
		}
	}

	/* If we are carrying video, and both sides are not going to remotely bridge... fail the native bridge */
	if (glue0->video.result != AST_RTP_GLUE_RESULT_FORBID
		&& (glue0->audio.result != AST_RTP_GLUE_RESULT_REMOTE
			|| glue0->video.result != AST_RTP_GLUE_RESULT_REMOTE)) {
		glue0->audio.result = AST_RTP_GLUE_RESULT_FORBID;
	}
	if (glue1->video.result != AST_RTP_GLUE_RESULT_FORBID
		&& (glue1->audio.result != AST_RTP_GLUE_RESULT_REMOTE
			|| glue1->video.result != AST_RTP_GLUE_RESULT_REMOTE)) {
		glue1->audio.result = AST_RTP_GLUE_RESULT_FORBID;
	}

	/* The order of preference is: forbid, local, and remote. */
	if (glue0->audio.result == AST_RTP_GLUE_RESULT_FORBID
		|| glue1->audio.result == AST_RTP_GLUE_RESULT_FORBID) {
		/* If any sort of bridge is forbidden just completely bail out and go back to generic bridging */
		combined_result = AST_RTP_GLUE_RESULT_FORBID;
	} else if (glue0->audio.result == AST_RTP_GLUE_RESULT_LOCAL
		|| glue1->audio.result == AST_RTP_GLUE_RESULT_LOCAL) {
		combined_result = AST_RTP_GLUE_RESULT_LOCAL;
	} else {
		combined_result = AST_RTP_GLUE_RESULT_REMOTE;
	}
	glue0->result = combined_result;
	glue1->result = combined_result;

	return 0;
}

/*!
 * \internal
 * \brief Get the current RTP native bridge combined glue result.
 * \since 15.0.0
 *
 * \param c0 First bridge channel
 * \param c1 Second bridge channel
 *
 * \note Both channels must be locked when calling this function.
 *
 * \return Current combined glue result.
 */
static enum ast_rtp_glue_result rtp_glue_get_current_combined_result(struct ast_channel *c0,
	struct ast_channel *c1)
{
	struct rtp_glue_data glue_a;
	struct rtp_glue_data glue_b;
	struct rtp_glue_data *glue0;
	struct rtp_glue_data *glue1;
	enum ast_rtp_glue_result combined_result;

	rtp_glue_data_init(&glue_a);
	glue0 = &glue_a;
	rtp_glue_data_init(&glue_b);
	glue1 = &glue_b;
	if (rtp_glue_data_get(c0, glue0, c1, glue1)) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	combined_result = glue0->result;
	rtp_glue_data_destroy(glue0);
	rtp_glue_data_destroy(glue1);
	return combined_result;
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
	struct native_rtp_bridge_channel_data *data0;
	struct native_rtp_bridge_channel_data *data1;
	struct rtp_glue_data *glue0;
	struct rtp_glue_data *glue1;
	struct ast_format_cap *cap0;
	struct ast_format_cap *cap1;
	enum ast_rtp_glue_result native_type;

	if (bc0 == bc1) {
		return;
	}
	data0 = bc0->tech_pvt;
	data1 = bc1->tech_pvt;
	if (!data0 || !data1) {
		/* Not all channels are joined with the bridge tech yet */
		return;
	}
	glue0 = &data0->glue;
	glue1 = &data1->glue;

	ast_channel_lock_both(bc0->chan, bc1->chan);

	if (!glue0->cb || !glue1->cb) {
		/*
		 * Somebody doesn't have glue data so the bridge isn't running
		 *
		 * Actually neither side should have glue data.
		 */
		ast_assert(!glue0->cb && !glue1->cb);

		if (rtp_glue_data_get(bc0->chan, glue0, bc1->chan, glue1)) {
			/*
			 * This might happen if one of the channels got masqueraded
			 * at a critical time.  It's a bit of a stretch even then
			 * since the channel is in a bridge.
			 */
			goto done;
		}
	}

	ast_debug(2, "Bridge '%s'.  Tech starting '%s' and '%s' with target '%s'\n",
		bridge->uniqueid, ast_channel_name(bc0->chan), ast_channel_name(bc1->chan),
		target ? ast_channel_name(target) : "none");

	native_type = glue0->result;

	switch (native_type) {
	case AST_RTP_GLUE_RESULT_LOCAL:
		if (ast_rtp_instance_get_engine(glue0->audio.instance)->local_bridge) {
			ast_rtp_instance_get_engine(glue0->audio.instance)->local_bridge(glue0->audio.instance, glue1->audio.instance);
		}
		if (ast_rtp_instance_get_engine(glue1->audio.instance)->local_bridge) {
			ast_rtp_instance_get_engine(glue1->audio.instance)->local_bridge(glue1->audio.instance, glue0->audio.instance);
		}
		ast_rtp_instance_set_bridged(glue0->audio.instance, glue1->audio.instance);
		ast_rtp_instance_set_bridged(glue1->audio.instance, glue0->audio.instance);
		ast_verb(4, "Locally RTP bridged '%s' and '%s' in stack\n",
			ast_channel_name(bc0->chan), ast_channel_name(bc1->chan));
		break;
	case AST_RTP_GLUE_RESULT_REMOTE:
		cap0 = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		cap1 = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (!cap0 || !cap1) {
			ao2_cleanup(cap0);
			ao2_cleanup(cap1);
			break;
		}

		if (glue0->cb->get_codec) {
			glue0->cb->get_codec(bc0->chan, cap0);
		}
		if (glue1->cb->get_codec) {
			glue1->cb->get_codec(bc1->chan, cap1);
		}

		/*
		 * If we have a target, it's the channel that received the UNHOLD or
		 * UPDATE_RTP_PEER frame and was told to resume
		 */
		if (!target) {
			/* Send both channels to remote */
			data0->remote_cb = glue0->cb;
			data1->remote_cb = glue1->cb;
			glue0->cb->update_peer(bc0->chan, glue1->audio.instance, glue1->video.instance, NULL, cap1, 0);
			glue1->cb->update_peer(bc1->chan, glue0->audio.instance, glue0->video.instance, NULL, cap0, 0);
			ast_verb(4, "Remotely bridged '%s' and '%s' - media will flow directly between them\n",
				ast_channel_name(bc0->chan), ast_channel_name(bc1->chan));
		} else {
			/*
			 * If a target was provided, it is the recipient of an unhold or an update and needs to have
			 * its media redirected to fit the current remote bridging needs. The other channel is either
			 * already set up to handle the new media path or will have its own set of updates independent
			 * of this pass.
			 */
			ast_debug(2, "Bridge '%s'.  Sending '%s' back to remote\n",
				bridge->uniqueid, ast_channel_name(target));
			if (bc0->chan == target) {
				data0->remote_cb = glue0->cb;
				glue0->cb->update_peer(bc0->chan, glue1->audio.instance, glue1->video.instance, NULL, cap1, 0);
			} else {
				data1->remote_cb = glue1->cb;
				glue1->cb->update_peer(bc1->chan, glue0->audio.instance, glue0->video.instance, NULL, cap0, 0);
			}
		}

		ao2_cleanup(cap0);
		ao2_cleanup(cap1);
		break;
	case AST_RTP_GLUE_RESULT_FORBID:
		break;
	}

	if (native_type != AST_RTP_GLUE_RESULT_REMOTE) {
		/* Bring any remaining channels back to us. */
		if (data0->remote_cb) {
			ast_debug(2, "Bridge '%s'.  Bringing back '%s' to us\n",
				bridge->uniqueid, ast_channel_name(bc0->chan));
			data0->remote_cb->update_peer(bc0->chan, NULL, NULL, NULL, NULL, 0);
			data0->remote_cb = NULL;
		}
		if (data1->remote_cb) {
			ast_debug(2, "Bridge '%s'.  Bringing back '%s' to us\n",
				bridge->uniqueid, ast_channel_name(bc1->chan));
			data1->remote_cb->update_peer(bc1->chan, NULL, NULL, NULL, NULL, 0);
			data1->remote_cb = NULL;
		}
	}

done:
	ast_channel_unlock(bc0->chan);
	ast_channel_unlock(bc1->chan);
}

/*!
 * \internal
 * \brief Stop native RTP bridging of two channels
 *
 * \param bridge The bridge that had native RTP bridging happening on it
 * \param target If remote RTP bridging, the channel that is held.
 *
 * \note The first channel to leave the bridge triggers the cleanup for both channels
 */
static void native_rtp_bridge_stop(struct ast_bridge *bridge, struct ast_channel *target)
{
	struct ast_bridge_channel *bc0 = AST_LIST_FIRST(&bridge->channels);
	struct ast_bridge_channel *bc1 = AST_LIST_LAST(&bridge->channels);
	struct native_rtp_bridge_channel_data *data0;
	struct native_rtp_bridge_channel_data *data1;
	struct rtp_glue_data *glue0;
	struct rtp_glue_data *glue1;

	if (bc0 == bc1) {
		return;
	}
	data0 = bc0->tech_pvt;
	data1 = bc1->tech_pvt;
	if (!data0 || !data1) {
		/* Not all channels are joined with the bridge tech */
		return;
	}
	glue0 = &data0->glue;
	glue1 = &data1->glue;

	ast_debug(2, "Bridge '%s'.  Tech stopping '%s' and '%s' with target '%s'\n",
		bridge->uniqueid, ast_channel_name(bc0->chan), ast_channel_name(bc1->chan),
		target ? ast_channel_name(target) : "none");

	if (!glue0->cb || !glue1->cb) {
		/*
		 * Somebody doesn't have glue data so the bridge isn't running
		 *
		 * Actually neither side should have glue data.
		 */
		ast_assert(!glue0->cb && !glue1->cb);
		/* At most one channel can be left at the remote endpoint here. */
		ast_assert(!data0->remote_cb || !data1->remote_cb);

		/* Bring selected channel streams back to us */
		if (data0->remote_cb && (!target || target == bc0->chan)) {
			ast_channel_lock(bc0->chan);
			ast_debug(2, "Bridge '%s'.  Bringing back '%s' to us\n",
				bridge->uniqueid, ast_channel_name(bc0->chan));
			data0->remote_cb->update_peer(bc0->chan, NULL, NULL, NULL, NULL, 0);
			data0->remote_cb = NULL;
			ast_channel_unlock(bc0->chan);
		}
		if (data1->remote_cb && (!target || target == bc1->chan)) {
			ast_channel_lock(bc1->chan);
			ast_debug(2, "Bridge '%s'.  Bringing back '%s' to us\n",
				bridge->uniqueid, ast_channel_name(bc1->chan));
			data1->remote_cb->update_peer(bc1->chan, NULL, NULL, NULL, NULL, 0);
			data1->remote_cb = NULL;
			ast_channel_unlock(bc1->chan);
		}
		return;
	}

	ast_channel_lock_both(bc0->chan, bc1->chan);

	switch (glue0->result) {
	case AST_RTP_GLUE_RESULT_LOCAL:
		if (ast_rtp_instance_get_engine(glue0->audio.instance)->local_bridge) {
			ast_rtp_instance_get_engine(glue0->audio.instance)->local_bridge(glue0->audio.instance, NULL);
		}
		if (ast_rtp_instance_get_engine(glue1->audio.instance)->local_bridge) {
			ast_rtp_instance_get_engine(glue1->audio.instance)->local_bridge(glue1->audio.instance, NULL);
		}
		ast_rtp_instance_set_bridged(glue0->audio.instance, NULL);
		ast_rtp_instance_set_bridged(glue1->audio.instance, NULL);
		break;
	case AST_RTP_GLUE_RESULT_REMOTE:
		if (target) {
			/*
			 * If a target was provided, it is being put on hold and should expect to
			 * receive media from Asterisk instead of what it was previously connected to.
			 */
			ast_debug(2, "Bridge '%s'.  Bringing back '%s' to us\n",
				bridge->uniqueid, ast_channel_name(target));
			if (bc0->chan == target) {
				data0->remote_cb = NULL;
				glue0->cb->update_peer(bc0->chan, NULL, NULL, NULL, NULL, 0);
			} else {
				data1->remote_cb = NULL;
				glue1->cb->update_peer(bc1->chan, NULL, NULL, NULL, NULL, 0);
			}
		} else {
			data0->remote_cb = NULL;
			data1->remote_cb = NULL;
			/*
			 * XXX We don't want to bring back the channels if we are
			 * switching to T.38.  We have received a reinvite on one channel
			 * and we will be sending a reinvite on the other to start T.38.
			 * If we bring the streams back now we confuse the chan_pjsip
			 * channel driver processing the incoming T.38 reinvite with
			 * reinvite glare.  I think this is really a bug in chan_pjsip
			 * that this exception case is working around.
			 */
			if (rtp_glue_get_current_combined_result(bc0->chan, bc1->chan)
				!= AST_RTP_GLUE_RESULT_FORBID) {
				ast_debug(2, "Bridge '%s'.  Bringing back '%s' and '%s' to us\n",
					bridge->uniqueid, ast_channel_name(bc0->chan),
					ast_channel_name(bc1->chan));
				glue0->cb->update_peer(bc0->chan, NULL, NULL, NULL, NULL, 0);
				glue1->cb->update_peer(bc1->chan, NULL, NULL, NULL, NULL, 0);
			} else {
				ast_debug(2, "Bridge '%s'.  Skip bringing back '%s' and '%s' to us\n",
					bridge->uniqueid, ast_channel_name(bc0->chan),
					ast_channel_name(bc1->chan));
			}
		}
		break;
	case AST_RTP_GLUE_RESULT_FORBID:
		break;
	}

	rtp_glue_data_reset(glue0);
	rtp_glue_data_reset(glue1);

	ast_debug(2, "Discontinued RTP bridging of '%s' and '%s' - media will flow through Asterisk core\n",
		ast_channel_name(bc0->chan), ast_channel_name(bc1->chan));

	ast_channel_unlock(bc0->chan);
	ast_channel_unlock(bc1->chan);
}

/*!
 * \internal
 * \brief Frame hook that is called to intercept hold/unhold
 */
static struct ast_frame *native_rtp_framehook(struct ast_channel *chan,
	struct ast_frame *f, enum ast_framehook_event event, void *data)
{
	struct ast_bridge *bridge;
	struct native_rtp_framehook_data *native_data = data;

	if (!f
		|| f->frametype != AST_FRAME_CONTROL
		|| event != AST_FRAMEHOOK_EVENT_WRITE) {
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
			switch (f->subclass.integer) {
			case AST_CONTROL_HOLD:
				native_rtp_bridge_stop(bridge, chan);
				break;
			case AST_CONTROL_UNHOLD:
			case AST_CONTROL_UPDATE_RTP_PEER:
				native_rtp_bridge_start(bridge, chan);
				break;
			default:
				break;
			}
		}
		ast_bridge_unlock(bridge);
		ao2_ref(bridge, -1);
		ast_channel_lock(chan);
	}

	return f;
}

/*!
 * \internal
 * \brief Callback function which informs upstream if we are consuming a frame of a specific type
 */
static int native_rtp_framehook_consume(void *data, enum ast_frame_type type)
{
	return (type == AST_FRAME_CONTROL ? 1 : 0);
}

/*!
 * \internal
 * \brief Internal helper function which checks whether a channel is compatible with our native bridging
 */
static int native_rtp_bridge_capable(struct ast_channel *chan)
{
	return !ast_channel_has_hook_requiring_audio(chan)
			&& ast_channel_state(chan) == AST_STATE_UP;
}

/*!
 * \internal
 * \brief Internal helper function which checks whether both channels are compatible with our native bridging
 */
static int native_rtp_bridge_compatible_check(struct ast_bridge *bridge, struct ast_bridge_channel *bc0, struct ast_bridge_channel *bc1)
{
	enum ast_rtp_glue_result native_type;
	int read_ptime0;
	int read_ptime1;
	int write_ptime0;
	int write_ptime1;
	struct rtp_glue_data glue_a;
	struct rtp_glue_data glue_b;
	RAII_VAR(struct ast_format_cap *, cap0, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, cap1, NULL, ao2_cleanup);
	RAII_VAR(struct rtp_glue_data *, glue0, NULL, rtp_glue_data_destroy);
	RAII_VAR(struct rtp_glue_data *, glue1, NULL, rtp_glue_data_destroy);

	ast_debug(1, "Bridge '%s'.  Checking compatability for channels '%s' and '%s'\n",
		bridge->uniqueid, ast_channel_name(bc0->chan), ast_channel_name(bc1->chan));

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

	rtp_glue_data_init(&glue_a);
	glue0 = &glue_a;
	rtp_glue_data_init(&glue_b);
	glue1 = &glue_b;
	if (rtp_glue_data_get(bc0->chan, glue0, bc1->chan, glue1)) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as could not get details\n",
			bridge->uniqueid);
		return 0;
	}
	native_type = glue0->result;

	if (native_type == AST_RTP_GLUE_RESULT_FORBID) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as it was forbidden while getting details\n",
			bridge->uniqueid);
		return 0;
	}

	if (ao2_container_count(bc0->features->dtmf_hooks)
		&& ast_rtp_instance_dtmf_mode_get(glue0->audio.instance)) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as channel '%s' has DTMF hooks\n",
			bridge->uniqueid, ast_channel_name(bc0->chan));
		return 0;
	}

	if (ao2_container_count(bc1->features->dtmf_hooks)
		&& ast_rtp_instance_dtmf_mode_get(glue1->audio.instance)) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as channel '%s' has DTMF hooks\n",
			bridge->uniqueid, ast_channel_name(bc1->chan));
		return 0;
	}

	if (native_type == AST_RTP_GLUE_RESULT_LOCAL
		&& (ast_rtp_instance_get_engine(glue0->audio.instance)->local_bridge
			!= ast_rtp_instance_get_engine(glue1->audio.instance)->local_bridge
			|| (ast_rtp_instance_get_engine(glue0->audio.instance)->dtmf_compatible
				&& !ast_rtp_instance_get_engine(glue0->audio.instance)->dtmf_compatible(bc0->chan,
					glue0->audio.instance, bc1->chan, glue1->audio.instance)))) {
		ast_debug(1, "Bridge '%s' can not use local native RTP bridge as local bridge or DTMF is not compatible\n",
			bridge->uniqueid);
		return 0;
	}

	cap0 = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	cap1 = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap0 || !cap1) {
		return 0;
	}

	/* Make sure that codecs match */
	if (glue0->cb->get_codec) {
		glue0->cb->get_codec(bc0->chan, cap0);
	}
	if (glue1->cb->get_codec) {
		glue1->cb->get_codec(bc1->chan, cap1);
	}
	if (ast_format_cap_count(cap0) != 0
		&& ast_format_cap_count(cap1) != 0
		&& !ast_format_cap_iscompatible(cap0, cap1)) {
		struct ast_str *codec_buf0 = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		struct ast_str *codec_buf1 = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);

		ast_debug(1, "Bridge '%s': Channel codec0 = %s is not codec1 = %s, cannot native bridge in RTP.\n",
			bridge->uniqueid,
			ast_format_cap_get_names(cap0, &codec_buf0),
			ast_format_cap_get_names(cap1, &codec_buf1));
		return 0;
	}

	if (glue0->audio.instance && glue1->audio.instance) {
		unsigned int framing_inst0, framing_inst1;
		framing_inst0 = ast_rtp_codecs_get_framing(ast_rtp_instance_get_codecs(glue0->audio.instance));
		framing_inst1 = ast_rtp_codecs_get_framing(ast_rtp_instance_get_codecs(glue1->audio.instance));
		if (framing_inst0 != framing_inst1) {
			/* ptimes are asymmetric on the two call legs so we can't use the native bridge */
			ast_debug(1, "Asymmetric ptimes on the two call legs (%u != %u). Cannot native bridge in RTP\n",
				framing_inst0, framing_inst1);
			return 0;
		}
	}

	read_ptime0 = ast_format_cap_get_format_framing(cap0, ast_channel_rawreadformat(bc0->chan));
	read_ptime1 = ast_format_cap_get_format_framing(cap1, ast_channel_rawreadformat(bc1->chan));
	write_ptime0 = ast_format_cap_get_format_framing(cap0, ast_channel_rawwriteformat(bc0->chan));
	write_ptime1 = ast_format_cap_get_format_framing(cap1, ast_channel_rawwriteformat(bc1->chan));

	if (read_ptime0 != write_ptime1 || read_ptime1 != write_ptime0) {
		ast_debug(1, "Bridge '%s': Packetization differs between RTP streams (%d != %d or %d != %d). Cannot native bridge in RTP\n",
			bridge->uniqueid,
			read_ptime0, write_ptime1, read_ptime1, write_ptime0);
		return 0;
	}

	return 1;
}

/*!
 * \internal
 * \brief Called by the bridge core "compatible' callback
 */
static int native_rtp_bridge_compatible(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *bc0;
	struct ast_bridge_channel *bc1;
	int is_compatible;

	/* We require two channels before even considering native bridging */
	if (bridge->num_channels != 2) {
		ast_debug(1, "Bridge '%s' can not use native RTP bridge as two channels are required\n",
			bridge->uniqueid);
		return 0;
	}

	bc0 = AST_LIST_FIRST(&bridge->channels);
	bc1 = AST_LIST_LAST(&bridge->channels);

	ast_channel_lock_both(bc0->chan, bc1->chan);
	is_compatible = native_rtp_bridge_compatible_check(bridge, bc0, bc1);
	ast_channel_unlock(bc0->chan);
	ast_channel_unlock(bc1->chan);

	return is_compatible;
}

/*!
 * \internal
 * \brief Helper function which adds frame hook to bridge channel
 */
static int native_rtp_bridge_framehook_attach(struct ast_bridge_channel *bridge_channel)
{
	struct native_rtp_bridge_channel_data *data = bridge_channel->tech_pvt;
	struct ast_framehook_interface hook = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = native_rtp_framehook,
		.destroy_cb = __ao2_cleanup,
		.consume_cb = native_rtp_framehook_consume,
		.disable_inheritance = 1,
	};

	ast_assert(data->hook_data == NULL);
	data->hook_data = ao2_alloc_options(sizeof(*data->hook_data), NULL,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!data->hook_data) {
		return -1;
	}

	ast_debug(2, "Bridge '%s'.  Attaching hook data %p to '%s'\n",
		bridge_channel->bridge->uniqueid, data, ast_channel_name(bridge_channel->chan));

	/* We're giving 1 ref to the framehook and keeping the one from the alloc for ourselves */
	hook.data = ao2_bump(data->hook_data);

	ast_channel_lock(bridge_channel->chan);
	data->hook_data->id = ast_framehook_attach(bridge_channel->chan, &hook);
	ast_channel_unlock(bridge_channel->chan);
	if (data->hook_data->id < 0) {
		/*
		 * We need to drop both the reference we hold in data,
		 * and the one the framehook would hold.
		 */
		ao2_ref(data->hook_data, -2);
		data->hook_data = NULL;

		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Helper function which removes frame hook from bridge channel
 */
static void native_rtp_bridge_framehook_detach(struct ast_bridge_channel *bridge_channel)
{
	struct native_rtp_bridge_channel_data *data = bridge_channel->tech_pvt;

	if (!data || !data->hook_data) {
		return;
	}

	ast_debug(2, "Bridge '%s'.  Detaching hook data %p from '%s'\n",
		bridge_channel->bridge->uniqueid, data->hook_data, ast_channel_name(bridge_channel->chan));

	ast_channel_lock(bridge_channel->chan);
	ast_framehook_detach(bridge_channel->chan, data->hook_data->id);
	data->hook_data->detached = 1;
	ast_channel_unlock(bridge_channel->chan);
	ao2_cleanup(data->hook_data);
	data->hook_data = NULL;
}

/*!
 * \internal
 * \brief Called by the bridge core 'join' callback for each channel joining he bridge
 */
static int native_rtp_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	ast_debug(2, "Bridge '%s'.  Channel '%s' is joining bridge tech\n",
		bridge->uniqueid, ast_channel_name(bridge_channel->chan));

	ast_assert(bridge_channel->tech_pvt == NULL);

	if (bridge_channel->suspended) {
		/* The channel will rejoin when it is unsuspended */
		return 0;
	}

	bridge_channel->tech_pvt = native_rtp_bridge_channel_data_alloc();
	if (!bridge_channel->tech_pvt) {
		return -1;
	}

	if (native_rtp_bridge_framehook_attach(bridge_channel)) {
		native_rtp_bridge_channel_data_free(bridge_channel->tech_pvt);
		bridge_channel->tech_pvt = NULL;
		return -1;
	}

	native_rtp_bridge_start(bridge, NULL);
	return 0;
}

/*!
 * \internal
 * \brief Add the channel back into the bridge
 */
static void native_rtp_bridge_unsuspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	ast_debug(2, "Bridge '%s'.  Channel '%s' is unsuspended back to bridge tech\n",
		bridge->uniqueid, ast_channel_name(bridge_channel->chan));
	native_rtp_bridge_join(bridge, bridge_channel);
}

/*!
 * \internal
 * \brief Leave the bridge
 */
static void native_rtp_bridge_leave(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	ast_debug(2, "Bridge '%s'.  Channel '%s' is leaving bridge tech\n",
		bridge->uniqueid, ast_channel_name(bridge_channel->chan));

	if (!bridge_channel->tech_pvt) {
		return;
	}

	native_rtp_bridge_framehook_detach(bridge_channel);
	native_rtp_bridge_stop(bridge, NULL);

	native_rtp_bridge_channel_data_free(bridge_channel->tech_pvt);
	bridge_channel->tech_pvt = NULL;
}

/*!
 * \internal
 * \brief Suspend the channel from the bridge
 */
static void native_rtp_bridge_suspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	ast_debug(2, "Bridge '%s'.  Channel '%s' is suspending from bridge tech\n",
		bridge->uniqueid, ast_channel_name(bridge_channel->chan));
	native_rtp_bridge_leave(bridge, bridge_channel);
}

static int native_rtp_bridge_write(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	const struct ast_control_t38_parameters *t38_parameters;
	int defer = 0;

	if (!ast_bridge_queue_everyone_else(bridge, bridge_channel, frame)) {
		/* This frame was successfully queued so no need to defer */
		return 0;
	}

	/* Depending on the frame defer it so when the next channel joins it receives it */
	switch (frame->frametype) {
	case AST_FRAME_CONTROL:
		switch (frame->subclass.integer) {
		case AST_CONTROL_T38_PARAMETERS:
			t38_parameters = frame->data.ptr;
			switch (t38_parameters->request_response) {
			case AST_T38_REQUEST_NEGOTIATE:
				defer = -1;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return defer;
}

static struct ast_bridge_technology native_rtp_bridge = {
	.name = "native_rtp",
	.capabilities = AST_BRIDGE_CAPABILITY_NATIVE,
	.preference = AST_BRIDGE_PREFERENCE_BASE_NATIVE,
	.join = native_rtp_bridge_join,
	.unsuspend = native_rtp_bridge_unsuspend,
	.leave = native_rtp_bridge_leave,
	.suspend = native_rtp_bridge_suspend,
	.write = native_rtp_bridge_write,
	.compatible = native_rtp_bridge_compatible,
};

static int unload_module(void)
{
	ast_bridge_technology_unregister(&native_rtp_bridge);
	return 0;
}

static int load_module(void)
{
	if (ast_bridge_technology_register(&native_rtp_bridge)) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Native RTP bridging module");
