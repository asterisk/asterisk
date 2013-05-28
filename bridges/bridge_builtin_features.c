/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
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
 * \brief Built in bridging features
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
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/astobj2.h"
#include "asterisk/pbx.h"
#include "asterisk/parking.h"

/*!
 * \brief Helper function that presents dialtone and grabs extension
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int grab_transfer(struct ast_channel *chan, char *exten, size_t exten_len, const char *context)
{
	int res;

	/* Play the simple "transfer" prompt out and wait */
	res = ast_stream_and_wait(chan, "pbx-transfer", AST_DIGIT_ANY);
	ast_stopstream(chan);
	if (res < 0) {
		/* Hangup or error */
		return -1;
	}
	if (res) {
		/* Store the DTMF digit that interrupted playback of the file. */
		exten[0] = res;
	}

	/* Drop to dialtone so they can enter the extension they want to transfer to */
/* BUGBUG the timeout needs to be configurable from features.conf. */
	res = ast_app_dtget(chan, context, exten, exten_len, exten_len - 1, 3000);
	if (res < 0) {
		/* Hangup or error */
		res = -1;
	} else if (!res) {
		/* 0 for invalid extension dialed. */
		if (ast_strlen_zero(exten)) {
			ast_debug(1, "%s dialed no digits.\n", ast_channel_name(chan));
		} else {
			ast_debug(1, "%s dialed '%s@%s' does not exist.\n",
				ast_channel_name(chan), exten, context);
		}
		ast_stream_and_wait(chan, "pbx-invalid", AST_DIGIT_NONE);
		res = -1;
	} else {
		/* Dialed extension is valid. */
		res = 0;
	}
	return res;
}

static void copy_caller_data(struct ast_channel *dest, struct ast_channel *caller)
{
	ast_channel_lock_both(caller, dest);
	ast_connected_line_copy_from_caller(ast_channel_connected(dest), ast_channel_caller(caller));
	ast_channel_inherit_variables(caller, dest);
	ast_channel_datastore_inherit(caller, dest);
	ast_channel_unlock(dest);
	ast_channel_unlock(caller);
}

/*! \brief Helper function that creates an outgoing channel and returns it immediately */
static struct ast_channel *dial_transfer(struct ast_channel *caller, const char *exten, const char *context)
{
	char destination[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 1];
	struct ast_channel *chan;
	int cause;

	/* Fill the variable with the extension and context we want to call */
	snprintf(destination, sizeof(destination), "%s@%s", exten, context);

	/* Now we request a local channel to prepare to call the destination */
	chan = ast_request("Local", ast_channel_nativeformats(caller), caller, destination,
		&cause);
	if (!chan) {
		return NULL;
	}

	/* Before we actually dial out let's inherit appropriate information. */
	copy_caller_data(chan, caller);

	/* Since the above worked fine now we actually call it and return the channel */
	if (ast_call(chan, destination, 0)) {
		ast_hangup(chan);
		return NULL;
	}

	return chan;
}

/*!
 * \internal
 * \brief Determine the transfer context to use.
 * \since 12.0.0
 *
 * \param transferer Channel initiating the transfer.
 * \param context User supplied context if available.  May be NULL.
 *
 * \return The context to use for the transfer.
 */
static const char *get_transfer_context(struct ast_channel *transferer, const char *context)
{
	if (!ast_strlen_zero(context)) {
		return context;
	}
	context = pbx_builtin_getvar_helper(transferer, "TRANSFER_CONTEXT");
	if (!ast_strlen_zero(context)) {
		return context;
	}
	context = ast_channel_macrocontext(transferer);
	if (!ast_strlen_zero(context)) {
		return context;
	}
	context = ast_channel_context(transferer);
	if (!ast_strlen_zero(context)) {
		return context;
	}
	return "default";
}

static void blind_transfer_cb(struct ast_channel *new_channel, void *user_data,
		enum ast_transfer_type transfer_type)
{
	struct ast_channel *transferer_channel = user_data;

	if (transfer_type == AST_BRIDGE_TRANSFER_MULTI_PARTY) {
		copy_caller_data(new_channel, transferer_channel);
	}
}

/*! \brief Internal built in feature for blind transfers */
static int feature_blind_transfer(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	char exten[AST_MAX_EXTENSION] = "";
	struct ast_bridge_features_blind_transfer *blind_transfer = hook_pvt;
	const char *context;
	char *goto_on_blindxfr;

/* BUGBUG the peer needs to be put on hold for the transfer. */
	ast_channel_lock(bridge_channel->chan);
	context = ast_strdupa(get_transfer_context(bridge_channel->chan,
		blind_transfer ? blind_transfer->context : NULL));
	goto_on_blindxfr = ast_strdupa(S_OR(pbx_builtin_getvar_helper(bridge_channel->chan,
		"GOTO_ON_BLINDXFR"), ""));
	ast_channel_unlock(bridge_channel->chan);

	/* Grab the extension to transfer to */
	if (grab_transfer(bridge_channel->chan, exten, sizeof(exten), context)) {
		return 0;
	}

	if (!ast_strlen_zero(goto_on_blindxfr)) {
		ast_debug(1, "After transfer, transferer %s goes to %s\n",
				ast_channel_name(bridge_channel->chan), goto_on_blindxfr);
		ast_after_bridge_set_go_on(bridge_channel->chan, NULL, NULL, 0, goto_on_blindxfr);
	}

	if (ast_bridge_transfer_blind(bridge_channel->chan, exten, context, blind_transfer_cb,
			bridge_channel->chan) != AST_BRIDGE_TRANSFER_SUCCESS &&
			!ast_strlen_zero(goto_on_blindxfr)) {
		ast_after_bridge_goto_discard(bridge_channel->chan);
	}

	return 0;
}

/*! Attended transfer code */
enum atxfer_code {
	/*! Party C hungup or other reason to abandon the transfer. */
	ATXFER_INCOMPLETE,
	/*! Transfer party C to party A. */
	ATXFER_COMPLETE,
	/*! Turn the transfer into a threeway call. */
	ATXFER_THREEWAY,
	/*! Hangup party C and return party B to the bridge. */
	ATXFER_ABORT,
};

/*! \brief Attended transfer feature to complete transfer */
static int attended_transfer_complete(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	enum atxfer_code *transfer_code = hook_pvt;

	*transfer_code = ATXFER_COMPLETE;
	ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
	return 0;
}

/*! \brief Attended transfer feature to turn it into a threeway call */
static int attended_transfer_threeway(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	enum atxfer_code *transfer_code = hook_pvt;

	*transfer_code = ATXFER_THREEWAY;
	ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
	return 0;
}

/*! \brief Attended transfer feature to abort transfer */
static int attended_transfer_abort(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	enum atxfer_code *transfer_code = hook_pvt;

	*transfer_code = ATXFER_ABORT;
	ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
	return 0;
}

/*! \brief Internal built in feature for attended transfers */
static int feature_attended_transfer(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	char exten[AST_MAX_EXTENSION] = "";
	struct ast_channel *peer;
	struct ast_bridge *attended_bridge;
	struct ast_bridge_features caller_features;
	int xfer_failed;
	struct ast_bridge_features_attended_transfer *attended_transfer = hook_pvt;
	const char *context;
	enum atxfer_code transfer_code = ATXFER_INCOMPLETE;

	bridge = ast_bridge_channel_merge_inhibit(bridge_channel, +1);

/* BUGBUG the peer needs to be put on hold for the transfer. */
	ast_channel_lock(bridge_channel->chan);
	context = ast_strdupa(get_transfer_context(bridge_channel->chan,
		attended_transfer ? attended_transfer->context : NULL));
	ast_channel_unlock(bridge_channel->chan);

	/* Grab the extension to transfer to */
	if (grab_transfer(bridge_channel->chan, exten, sizeof(exten), context)) {
		ast_bridge_merge_inhibit(bridge, -1);
		ao2_ref(bridge, -1);
		return 0;
	}

	/* Get a channel that is the destination we wish to call */
	peer = dial_transfer(bridge_channel->chan, exten, context);
	if (!peer) {
		ast_bridge_merge_inhibit(bridge, -1);
		ao2_ref(bridge, -1);
/* BUGBUG beeperr needs to be configurable from features.conf */
		ast_stream_and_wait(bridge_channel->chan, "beeperr", AST_DIGIT_NONE);
		return 0;
	}

/* BUGBUG bridging API features does not support features.conf featuremap */
/* BUGBUG bridging API features does not support the features.conf atxfer bounce between C & B channels */
	/* Setup a DTMF menu to control the transfer. */
	if (ast_bridge_features_init(&caller_features)
		|| ast_bridge_hangup_hook(&caller_features,
			attended_transfer_complete, &transfer_code, NULL, 0)
		|| ast_bridge_dtmf_hook(&caller_features,
			attended_transfer && !ast_strlen_zero(attended_transfer->abort)
				? attended_transfer->abort : "*1",
			attended_transfer_abort, &transfer_code, NULL, 0)
		|| ast_bridge_dtmf_hook(&caller_features,
			attended_transfer && !ast_strlen_zero(attended_transfer->complete)
				? attended_transfer->complete : "*2",
			attended_transfer_complete, &transfer_code, NULL, 0)
		|| ast_bridge_dtmf_hook(&caller_features,
			attended_transfer && !ast_strlen_zero(attended_transfer->threeway)
				? attended_transfer->threeway : "*3",
			attended_transfer_threeway, &transfer_code, NULL, 0)) {
		ast_bridge_features_cleanup(&caller_features);
		ast_hangup(peer);
		ast_bridge_merge_inhibit(bridge, -1);
		ao2_ref(bridge, -1);
/* BUGBUG beeperr needs to be configurable from features.conf */
		ast_stream_and_wait(bridge_channel->chan, "beeperr", AST_DIGIT_NONE);
		return 0;
	}

	/* Create a bridge to use to talk to the person we are calling */
	attended_bridge = ast_bridge_base_new(AST_BRIDGE_CAPABILITY_1TO1MIX,
		AST_BRIDGE_FLAG_DISSOLVE_HANGUP);
	if (!attended_bridge) {
		ast_bridge_features_cleanup(&caller_features);
		ast_hangup(peer);
		ast_bridge_merge_inhibit(bridge, -1);
		ao2_ref(bridge, -1);
/* BUGBUG beeperr needs to be configurable from features.conf */
		ast_stream_and_wait(bridge_channel->chan, "beeperr", AST_DIGIT_NONE);
		return 0;
	}
	ast_bridge_merge_inhibit(attended_bridge, +1);

	/* This is how this is going down, we are imparting the channel we called above into this bridge first */
/* BUGBUG we should impart the peer as an independent and move it to the original bridge. */
	if (ast_bridge_impart(attended_bridge, peer, NULL, NULL, 0)) {
		ast_bridge_destroy(attended_bridge);
		ast_bridge_features_cleanup(&caller_features);
		ast_hangup(peer);
		ast_bridge_merge_inhibit(bridge, -1);
		ao2_ref(bridge, -1);
/* BUGBUG beeperr needs to be configurable from features.conf */
		ast_stream_and_wait(bridge_channel->chan, "beeperr", AST_DIGIT_NONE);
		return 0;
	}

	/*
	 * For the caller we want to join the bridge in a blocking
	 * fashion so we don't spin around in this function doing
	 * nothing while waiting.
	 */
	ast_bridge_join(attended_bridge, bridge_channel->chan, NULL, &caller_features, NULL, 0);

/*
 * BUGBUG there is a small window where the channel does not point to the bridge_channel.
 *
 * This window is expected to go away when atxfer is redesigned
 * to fully support existing functionality.  There will be one
 * and only one ast_bridge_channel structure per channel.
 */
	/* Point the channel back to the original bridge and bridge_channel. */
	ast_bridge_channel_lock(bridge_channel);
	ast_channel_lock(bridge_channel->chan);
	ast_channel_internal_bridge_channel_set(bridge_channel->chan, bridge_channel);
	ast_channel_internal_bridge_set(bridge_channel->chan, bridge_channel->bridge);
	ast_channel_unlock(bridge_channel->chan);
	ast_bridge_channel_unlock(bridge_channel);

	/* Wait for peer thread to exit bridge and die. */
	if (!ast_autoservice_start(bridge_channel->chan)) {
		ast_bridge_depart(peer);
		ast_autoservice_stop(bridge_channel->chan);
	} else {
		ast_bridge_depart(peer);
	}

	/* Now that all channels are out of it we can destroy the bridge and the feature structures */
	ast_bridge_destroy(attended_bridge);
	ast_bridge_features_cleanup(&caller_features);

	xfer_failed = -1;
	switch (transfer_code) {
	case ATXFER_INCOMPLETE:
		/* Peer hungup */
		break;
	case ATXFER_COMPLETE:
		/* The peer takes our place in the bridge. */
		ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
		xfer_failed = ast_bridge_impart(bridge_channel->bridge, peer, bridge_channel->chan, NULL, 1);
		break;
	case ATXFER_THREEWAY:
		/*
		 * Transferer wants to convert to a threeway call.
		 *
		 * Just impart the peer onto the bridge and have us return to it
		 * as normal.
		 */
		xfer_failed = ast_bridge_impart(bridge_channel->bridge, peer, NULL, NULL, 1);
		break;
	case ATXFER_ABORT:
		/* Transferer decided not to transfer the call after all. */
		break;
	}
	ast_bridge_merge_inhibit(bridge, -1);
	ao2_ref(bridge, -1);
	if (xfer_failed) {
		ast_hangup(peer);
		if (!ast_check_hangup_locked(bridge_channel->chan)) {
			ast_stream_and_wait(bridge_channel->chan, "beeperr", AST_DIGIT_NONE);
		}
	}

	return 0;
}

/*! \brief Internal built in feature for hangup */
static int feature_hangup(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	/*
	 * This is very simple, we simply change the state on the
	 * bridge_channel to force the channel out of the bridge and the
	 * core takes care of the rest.
	 */
	ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_END);
	return 0;
}

static int unload_module(void)
{
	return 0;
}

static int load_module(void)
{
	ast_bridge_features_register(AST_BRIDGE_BUILTIN_BLINDTRANSFER, feature_blind_transfer, NULL);
	ast_bridge_features_register(AST_BRIDGE_BUILTIN_ATTENDEDTRANSFER, feature_attended_transfer, NULL);
	ast_bridge_features_register(AST_BRIDGE_BUILTIN_HANGUP, feature_hangup, NULL);

	/* Bump up our reference count so we can't be unloaded */
	ast_module_ref(ast_module_info->self);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Built in bridging features");
