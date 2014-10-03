/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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

/*!
 * \file
 * \brief Unreal channel derivatives framework for channel drivers like local channels.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/causes.h"
#include "asterisk/channel.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/pbx.h"
#include "asterisk/musiconhold.h"
#include "asterisk/astobj2.h"
#include "asterisk/bridge.h"
#include "asterisk/core_unreal.h"

static unsigned int name_sequence = 0;

void ast_unreal_lock_all(struct ast_unreal_pvt *p, struct ast_channel **outchan, struct ast_channel **outowner)
{
	struct ast_channel *chan = NULL;
	struct ast_channel *owner = NULL;

	ao2_lock(p);
	for (;;) {
		if (p->chan) {
			chan = p->chan;
			ast_channel_ref(chan);
		}
		if (p->owner) {
			owner = p->owner;
			ast_channel_ref(owner);
		}
		ao2_unlock(p);

		/* if we don't have both channels, then this is very easy */
		if (!owner || !chan) {
			if (owner) {
				ast_channel_lock(owner);
			} else if(chan) {
				ast_channel_lock(chan);
			}
		} else {
			/* lock both channels first, then get the pvt lock */
			ast_channel_lock_both(chan, owner);
		}
		ao2_lock(p);

		/* Now that we have all the locks, validate that nothing changed */
		if (p->owner != owner || p->chan != chan) {
			if (owner) {
				ast_channel_unlock(owner);
				owner = ast_channel_unref(owner);
			}
			if (chan) {
				ast_channel_unlock(chan);
				chan = ast_channel_unref(chan);
			}
			continue;
		}

		break;
	}
	*outowner = p->owner;
	*outchan = p->chan;
}

/* Called with ast locked */
int ast_unreal_setoption(struct ast_channel *ast, int option, void *data, int datalen)
{
	int res = 0;
	struct ast_unreal_pvt *p;
	struct ast_channel *otherchan = NULL;
	ast_chan_write_info_t *write_info;
	char *info_data;

	if (option != AST_OPTION_CHANNEL_WRITE) {
		return -1;
	}

	write_info = data;

	if (write_info->version != AST_CHAN_WRITE_INFO_T_VERSION) {
		ast_log(LOG_ERROR, "The chan_write_info_t type has changed, and this channel hasn't been updated!\n");
		return -1;
	}

	info_data = write_info->data;
	if (!strcmp(write_info->function, "CHANNEL")) {
		if (!strncasecmp(info_data, "hangup_handler_", 15)) {
			/* Block CHANNEL(hangup_handler_xxx) writes to the other unreal channel. */
			return 0;
		}

		/* Crossover the accountcode and peeraccount to cross the unreal bridge. */
		if (!strcasecmp(info_data, "accountcode")) {
			info_data = "peeraccount";
		} else if (!strcasecmp(info_data, "peeraccount")) {
			info_data = "accountcode";
		}
	}

	/* get the tech pvt */
	if (!(p = ast_channel_tech_pvt(ast))) {
		return -1;
	}
	ao2_ref(p, 1);
	ast_channel_unlock(ast); /* Held when called, unlock before locking another channel */

	/* get the channel we are supposed to write to */
	ao2_lock(p);
	otherchan = (write_info->chan == p->owner) ? p->chan : p->owner;
	if (!otherchan || otherchan == write_info->chan) {
		res = -1;
		otherchan = NULL;
		ao2_unlock(p);
		goto setoption_cleanup;
	}
	ast_channel_ref(otherchan);

	/* clear the pvt lock before grabbing the channel */
	ao2_unlock(p);

	ast_channel_lock(otherchan);
	res = write_info->write_fn(otherchan, write_info->function, info_data, write_info->value);
	ast_channel_unlock(otherchan);

setoption_cleanup:
	ao2_ref(p, -1);
	if (otherchan) {
		ast_channel_unref(otherchan);
	}
	ast_channel_lock(ast); /* Lock back before we leave */
	return res;
}

/* Called with ast locked */
int ast_unreal_queryoption(struct ast_channel *ast, int option, void *data, int *datalen)
{
	struct ast_unreal_pvt *p;
	struct ast_channel *peer;
	struct ast_channel *other;
	int res = 0;

	if (option != AST_OPTION_T38_STATE) {
		/* AST_OPTION_T38_STATE is the only supported option at this time */
		return -1;
	}

	/* for some reason the channel is not locked in channel.c when this function is called */
	if (!(p = ast_channel_tech_pvt(ast))) {
		return -1;
	}

	ao2_lock(p);
	other = AST_UNREAL_IS_OUTBOUND(ast, p) ? p->owner : p->chan;
	if (!other) {
		ao2_unlock(p);
		return -1;
	}
	ast_channel_ref(other);
	ao2_unlock(p);
	ast_channel_unlock(ast); /* Held when called, unlock before locking another channel */

	peer = ast_channel_bridge_peer(other);
	if (peer) {
		res = ast_channel_queryoption(peer, option, data, datalen, 0);
		ast_channel_unref(peer);
	}
	ast_channel_unref(other);
	ast_channel_lock(ast); /* Lock back before we leave */

	return res;
}

/*!
 * \brief queue a frame onto either the p->owner or p->chan
 *
 * \note the ast_unreal_pvt MUST have it's ref count bumped before entering this function and
 * decremented after this function is called.  This is a side effect of the deadlock
 * avoidance that is necessary to lock 2 channels and a tech_pvt.  Without a ref counted
 * ast_unreal_pvt, it is impossible to guarantee it will not be destroyed by another thread
 * during deadlock avoidance.
 */
static int unreal_queue_frame(struct ast_unreal_pvt *p, int isoutbound, struct ast_frame *f,
	struct ast_channel *us, int us_locked)
{
	struct ast_channel *other;

	/* Recalculate outbound channel */
	other = isoutbound ? p->owner : p->chan;
	if (!other) {
		return 0;
	}

	/* do not queue media frames if a generator is on both unreal channels */
	if (us
		&& (f->frametype == AST_FRAME_VOICE || f->frametype == AST_FRAME_VIDEO)
		&& ast_channel_generator(us)
		&& ast_channel_generator(other)) {
		return 0;
	}

	/* grab a ref on the channel before unlocking the pvt,
	 * other can not go away from us now regardless of locking */
	ast_channel_ref(other);
	if (us && us_locked) {
		ast_channel_unlock(us);
	}
	ao2_unlock(p);

	if (f->frametype == AST_FRAME_CONTROL && f->subclass.integer == AST_CONTROL_RINGING) {
		ast_setstate(other, AST_STATE_RINGING);
	}
	ast_queue_frame(other, f);

	other = ast_channel_unref(other);
	if (us && us_locked) {
		ast_channel_lock(us);
	}
	ao2_lock(p);

	return 0;
}

int ast_unreal_answer(struct ast_channel *ast)
{
	struct ast_unreal_pvt *p = ast_channel_tech_pvt(ast);
	int isoutbound;
	int res = -1;

	if (!p) {
		return -1;
	}

	ao2_ref(p, 1);
	ao2_lock(p);
	isoutbound = AST_UNREAL_IS_OUTBOUND(ast, p);
	if (isoutbound) {
		/* Pass along answer since somebody answered us */
		struct ast_frame answer = { AST_FRAME_CONTROL, { AST_CONTROL_ANSWER } };

		res = unreal_queue_frame(p, isoutbound, &answer, ast, 1);
	} else {
		ast_log(LOG_WARNING, "Huh?  %s is being asked to answer?\n",
			ast_channel_name(ast));
	}
	ao2_unlock(p);
	ao2_ref(p, -1);
	return res;
}

/*!
 * \internal
 * \brief Check and optimize out the unreal channels between bridges.
 * \since 12.0.0
 *
 * \param ast Channel writing a frame into the unreal channels.
 * \param p Unreal channel private.
 *
 * \note It is assumed that ast is locked.
 * \note It is assumed that p is locked.
 *
 * \retval 0 if unreal channels were not optimized out.
 * \retval non-zero if unreal channels were optimized out.
 */
static int got_optimized_out(struct ast_channel *ast, struct ast_unreal_pvt *p)
{
	int res = 0;

	/* Do a few conditional checks early on just to see if this optimization is possible */
	if (ast_test_flag(p, AST_UNREAL_NO_OPTIMIZATION) || !p->chan || !p->owner) {
		return res;
	}

	if (ast == p->owner) {
		res = ast_bridge_unreal_optimize_out(p->owner, p->chan, p);
	} else if (ast == p->chan) {
		res = ast_bridge_unreal_optimize_out(p->chan, p->owner, p);
	}

	return res;
}

struct ast_frame  *ast_unreal_read(struct ast_channel *ast)
{
	return &ast_null_frame;
}

int ast_unreal_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct ast_unreal_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;

	if (!p) {
		return -1;
	}

	/* Just queue for delivery to the other side */
	ao2_ref(p, 1);
	ao2_lock(p);
	switch (f->frametype) {
	case AST_FRAME_VOICE:
	case AST_FRAME_VIDEO:
		if (got_optimized_out(ast, p)) {
			break;
		}
		/* fall through */
	default:
		res = unreal_queue_frame(p, AST_UNREAL_IS_OUTBOUND(ast, p), f, ast, 1);
		break;
	}
	ao2_unlock(p);
	ao2_ref(p, -1);

	return res;
}

int ast_unreal_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct ast_unreal_pvt *p = ast_channel_tech_pvt(newchan);
	struct ast_bridge *bridge_owner;
	struct ast_bridge *bridge_chan;

	if (!p) {
		return -1;
	}

	ao2_lock(p);

	if ((p->owner != oldchan) && (p->chan != oldchan)) {
		ast_log(LOG_WARNING, "Old channel %p wasn't %p or %p\n", oldchan, p->owner, p->chan);
		ao2_unlock(p);
		return -1;
	}
	if (p->owner == oldchan) {
		p->owner = newchan;
	} else {
		p->chan = newchan;
	}

	if (ast_check_hangup(newchan) || !p->owner || !p->chan) {
		ao2_unlock(p);
		return 0;
	}

	/* Do not let a masquerade cause an unreal channel to be bridged to itself! */
	bridge_owner = ast_channel_internal_bridge(p->owner);
	bridge_chan = ast_channel_internal_bridge(p->chan);
	if (bridge_owner && bridge_owner == bridge_chan) {
		ast_log(LOG_WARNING, "You can not bridge an unreal channel (%s) to itself!\n",
			ast_channel_name(newchan));
		ao2_unlock(p);
		ast_queue_hangup(newchan);
		return -1;
	}

	ao2_unlock(p);
	return 0;
}

/*!
 * \internal
 * \brief Queue up a frame representing the indication as a control frame.
 * \since 12.0.0
 *
 * \param p Unreal private structure.
 * \param ast Channel indicating the condition.
 * \param condition What is being indicated.
 * \param data Extra data.
 * \param datalen Length of extra data.
 *
 * \retval 0 on success.
 * \retval AST_T38_REQUEST_PARMS if successful and condition is AST_CONTROL_T38_PARAMETERS.
 * \retval -1 on error.
 */
static int unreal_queue_indicate(struct ast_unreal_pvt *p, struct ast_channel *ast, int condition, const void *data, size_t datalen)
{
	int res = 0;
	int isoutbound;

	ao2_lock(p);
	/*
	 * Block -1 stop tones events if we are to be optimized out.  We
	 * don't need a flurry of these events on an unreal channel chain
	 * when initially connected to slow the optimization process.
	 */
	if (0 <= condition || ast_test_flag(p, AST_UNREAL_NO_OPTIMIZATION)) {
		struct ast_frame f = {
			.frametype = AST_FRAME_CONTROL,
			.subclass.integer = condition,
			.data.ptr = (void *) data,
			.datalen = datalen,
		};

		isoutbound = AST_UNREAL_IS_OUTBOUND(ast, p);
		res = unreal_queue_frame(p, isoutbound, &f, ast, 1);
		if (!res
			&& condition == AST_CONTROL_T38_PARAMETERS
			&& datalen == sizeof(struct ast_control_t38_parameters)) {
			const struct ast_control_t38_parameters *parameters = data;

			if (parameters->request_response == AST_T38_REQUEST_PARMS) {
				res = AST_T38_REQUEST_PARMS;
			}
		}
	} else {
		ast_debug(4, "Blocked indication %d\n", condition);
	}
	ao2_unlock(p);

	return res;
}

/*!
 * \internal
 * \brief Handle COLP and redirecting conditions.
 * \since 12.0.0
 *
 * \param p Unreal private structure.
 * \param ast Channel indicating the condition.
 * \param condition What is being indicated.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int unreal_colp_redirect_indicate(struct ast_unreal_pvt *p, struct ast_channel *ast, int condition)
{
	struct ast_channel *my_chan;
	struct ast_channel *my_owner;
	struct ast_channel *this_channel;
	struct ast_channel *the_other_channel;
	int isoutbound;
	int res = 0;
	unsigned char frame_data[1024];
	struct ast_frame f = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = condition,
		.data.ptr = frame_data,
	};

	/*
	 * A connected line update frame may only contain a partial
	 * amount of data, such as just a source, or just a ton, and not
	 * the full amount of information.  However, the collected
	 * information is all stored in the outgoing channel's
	 * connectedline structure, so when receiving a connected line
	 * update on an outgoing unreal channel, we need to transmit the
	 * collected connected line information instead of whatever
	 * happens to be in this control frame.  The same applies for
	 * redirecting information, which is why it is handled here as
	 * well.
	 */
	ast_channel_unlock(ast);
	ast_unreal_lock_all(p, &my_chan, &my_owner);
	isoutbound = AST_UNREAL_IS_OUTBOUND(ast, p);
	if (isoutbound) {
		this_channel = p->chan;
		the_other_channel = p->owner;
	} else {
		this_channel = p->owner;
		the_other_channel = p->chan;
	}
	if (the_other_channel) {
		if (condition == AST_CONTROL_CONNECTED_LINE) {
			ast_connected_line_copy_to_caller(ast_channel_caller(the_other_channel),
				ast_channel_connected(this_channel));
			f.datalen = ast_connected_line_build_data(frame_data, sizeof(frame_data),
				ast_channel_connected(this_channel), NULL);
		} else {
			f.datalen = ast_redirecting_build_data(frame_data, sizeof(frame_data),
				ast_channel_redirecting(this_channel), NULL);
		}
	}
	if (my_chan) {
		ast_channel_unlock(my_chan);
		ast_channel_unref(my_chan);
	}
	if (my_owner) {
		ast_channel_unlock(my_owner);
		ast_channel_unref(my_owner);
	}
	if (the_other_channel) {
		res = unreal_queue_frame(p, isoutbound, &f, ast, 0);
	}
	ao2_unlock(p);
	ast_channel_lock(ast);

	return res;
}

int ast_unreal_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen)
{
	struct ast_unreal_pvt *p = ast_channel_tech_pvt(ast);
	int res = 0;

	if (!p) {
		return -1;
	}

	ao2_ref(p, 1); /* ref for unreal_queue_frame */

	switch (condition) {
	case AST_CONTROL_MASQUERADE_NOTIFY:
		/*
		 * Always block this because this is the channel being
		 * masqueraded; not anything down the chain.
		 */
		break;
	case AST_CONTROL_CONNECTED_LINE:
	case AST_CONTROL_REDIRECTING:
		res = unreal_colp_redirect_indicate(p, ast, condition);
		break;
	case AST_CONTROL_HOLD:
		if (ast_test_flag(p, AST_UNREAL_MOH_INTERCEPT)) {
			ast_moh_start(ast, data, NULL);
			break;
		}
		res = unreal_queue_indicate(p, ast, condition, data, datalen);
		break;
	case AST_CONTROL_UNHOLD:
		if (ast_test_flag(p, AST_UNREAL_MOH_INTERCEPT)) {
			ast_moh_stop(ast);
			break;
		}
		res = unreal_queue_indicate(p, ast, condition, data, datalen);
		break;
	default:
		res = unreal_queue_indicate(p, ast, condition, data, datalen);
		break;
	}

	ao2_ref(p, -1);
	return res;
}

int ast_unreal_digit_begin(struct ast_channel *ast, char digit)
{
	struct ast_unreal_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;
	struct ast_frame f = { AST_FRAME_DTMF_BEGIN, };
	int isoutbound;

	if (!p) {
		return -1;
	}

	ao2_ref(p, 1); /* ref for unreal_queue_frame */
	ao2_lock(p);
	isoutbound = AST_UNREAL_IS_OUTBOUND(ast, p);
	f.subclass.integer = digit;
	res = unreal_queue_frame(p, isoutbound, &f, ast, 0);
	ao2_unlock(p);
	ao2_ref(p, -1);

	return res;
}

int ast_unreal_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct ast_unreal_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;
	struct ast_frame f = { AST_FRAME_DTMF_END, };
	int isoutbound;

	if (!p) {
		return -1;
	}

	ao2_ref(p, 1); /* ref for unreal_queue_frame */
	ao2_lock(p);
	isoutbound = AST_UNREAL_IS_OUTBOUND(ast, p);
	f.subclass.integer = digit;
	f.len = duration;
	res = unreal_queue_frame(p, isoutbound, &f, ast, 0);
	ao2_unlock(p);
	ao2_ref(p, -1);

	return res;
}

int ast_unreal_sendtext(struct ast_channel *ast, const char *text)
{
	struct ast_unreal_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;
	struct ast_frame f = { AST_FRAME_TEXT, };
	int isoutbound;

	if (!p) {
		return -1;
	}

	ao2_ref(p, 1); /* ref for unreal_queue_frame */
	ao2_lock(p);
	isoutbound = AST_UNREAL_IS_OUTBOUND(ast, p);
	f.data.ptr = (char *) text;
	f.datalen = strlen(text) + 1;
	res = unreal_queue_frame(p, isoutbound, &f, ast, 0);
	ao2_unlock(p);
	ao2_ref(p, -1);
	return res;
}

int ast_unreal_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen)
{
	struct ast_unreal_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;
	struct ast_frame f = { AST_FRAME_HTML, };
	int isoutbound;

	if (!p) {
		return -1;
	}

	ao2_ref(p, 1); /* ref for unreal_queue_frame */
	ao2_lock(p);
	isoutbound = AST_UNREAL_IS_OUTBOUND(ast, p);
	f.subclass.integer = subclass;
	f.data.ptr = (char *)data;
	f.datalen = datalen;
	res = unreal_queue_frame(p, isoutbound, &f, ast, 0);
	ao2_unlock(p);
	ao2_ref(p, -1);

	return res;
}

void ast_unreal_call_setup(struct ast_channel *semi1, struct ast_channel *semi2)
{
	struct ast_var_t *varptr;
	struct ast_var_t *clone_var;

	ast_channel_stage_snapshot(semi2);

	/*
	 * Note that cid_num and cid_name aren't passed in the
	 * ast_channel_alloc calls in ast_unreal_new_channels().  It's
	 * done here instead.
	 */
	ast_party_redirecting_copy(ast_channel_redirecting(semi2), ast_channel_redirecting(semi1));

	ast_party_dialed_copy(ast_channel_dialed(semi2), ast_channel_dialed(semi1));

	/* Crossover the CallerID and conected-line to cross the unreal bridge. */
	ast_connected_line_copy_to_caller(ast_channel_caller(semi2), ast_channel_connected(semi1));
	ast_connected_line_copy_from_caller(ast_channel_connected(semi2), ast_channel_caller(semi1));

	ast_channel_language_set(semi2, ast_channel_language(semi1));

	/* Crossover the accountcode and peeraccount to cross the unreal bridge. */
	ast_channel_accountcode_set(semi2, ast_channel_peeraccount(semi1));
	ast_channel_peeraccount_set(semi2, ast_channel_accountcode(semi1));

	ast_channel_musicclass_set(semi2, ast_channel_musicclass(semi1));

	ast_channel_cc_params_init(semi2, ast_channel_get_cc_config_params(semi1));

	/*
	 * Make sure we inherit the AST_CAUSE_ANSWERED_ELSEWHERE if it's
	 * set on the queue/dial call request in the dialplan.
	 */
	if (ast_channel_hangupcause(semi1) == AST_CAUSE_ANSWERED_ELSEWHERE) {
		ast_channel_hangupcause_set(semi2, AST_CAUSE_ANSWERED_ELSEWHERE);
	}

	/*
	 * Copy the channel variables from the semi1 channel to the
	 * outgoing channel.
	 *
	 * Note that due to certain assumptions, they MUST be in the
	 * same order.
	 */
	AST_LIST_TRAVERSE(ast_channel_varshead(semi1), varptr, entries) {
		clone_var = ast_var_assign(varptr->name, varptr->value);
		if (clone_var) {
			AST_LIST_INSERT_TAIL(ast_channel_varshead(semi2), clone_var, entries);
		}
	}
	ast_channel_datastore_inherit(semi1, semi2);

	ast_channel_stage_snapshot_done(semi2);
}

int ast_unreal_channel_push_to_bridge(struct ast_channel *ast, struct ast_bridge *bridge, unsigned int flags)
{
	struct ast_bridge_features *features;
	struct ast_channel *chan;
	struct ast_channel *owner;
	RAII_VAR(struct ast_unreal_pvt *, p, NULL, ao2_cleanup);

	RAII_VAR(struct ast_callid *, bridge_callid, NULL, ast_callid_cleanup);

	ast_bridge_lock(bridge);
	bridge_callid = bridge->callid ? ast_callid_ref(bridge->callid) : NULL;
	ast_bridge_unlock(bridge);

	{
		SCOPED_CHANNELLOCK(lock, ast);
		p = ast_channel_tech_pvt(ast);
		if (!p) {
			return -1;
		}
		ao2_ref(p, +1);
	}

	{
		SCOPED_AO2LOCK(lock, p);
		chan = p->chan;
		if (!chan) {
			return -1;
		}

		owner = p->owner;
		if (!owner) {
			return -1;
		}

		ast_channel_ref(chan);
		ast_channel_ref(owner);
	}

	if (bridge_callid) {
		struct ast_callid *chan_callid;
		struct ast_callid *owner_callid;

		/* chan side call ID setting */
		ast_channel_lock(chan);

		chan_callid = ast_channel_callid(chan);
		if (!chan_callid) {
			ast_channel_callid_set(chan, bridge_callid);
		}
		ast_channel_unlock(chan);
		ast_callid_cleanup(chan_callid);

		/* owner side call ID setting */
		ast_channel_lock(owner);

		owner_callid = ast_channel_callid(owner);
		if (!owner_callid) {
			ast_channel_callid_set(owner, bridge_callid);
		}

		ast_channel_unlock(owner);
		ast_callid_cleanup(owner_callid);
	}

	/* We are done with the owner now that its call ID matches the bridge */
	ast_channel_unref(owner);
	owner = NULL;

	features = ast_bridge_features_new();
	if (!features) {
		ast_channel_unref(chan);
		return -1;
	}

	ast_set_flag(&features->feature_flags, flags);

	/* Impart the semi2 channel into the bridge */
	if (ast_bridge_impart(bridge, chan, NULL, features,
		AST_BRIDGE_IMPART_CHAN_INDEPENDENT)) {
		ast_bridge_features_destroy(features);
		ast_channel_unref(chan);
		return -1;
	}

	ao2_lock(p);
	ast_set_flag(p, AST_UNREAL_CARETAKER_THREAD);
	ao2_unlock(p);
	ast_channel_unref(chan);

	return 0;
}

int ast_unreal_hangup(struct ast_unreal_pvt *p, struct ast_channel *ast)
{
	int hangup_chan = 0;
	int res = 0;
	int cause;
	struct ast_channel *owner = NULL;
	struct ast_channel *chan = NULL;

	/* the pvt isn't going anywhere, it has a ref */
	ast_channel_unlock(ast);

	/* lock everything */
	ast_unreal_lock_all(p, &chan, &owner);

	if (ast != chan && ast != owner) {
		res = -1;
		goto unreal_hangup_cleanup;
	}

	cause = ast_channel_hangupcause(ast);

	if (ast == p->chan) {
		/* Outgoing side is hanging up. */
		ast_clear_flag(p, AST_UNREAL_CARETAKER_THREAD);
		p->chan = NULL;
		if (p->owner) {
			const char *status = pbx_builtin_getvar_helper(p->chan, "DIALSTATUS");

			if (status) {
				ast_channel_hangupcause_set(p->owner, cause);
				pbx_builtin_setvar_helper(p->owner, "CHANLOCALSTATUS", status);
			}
			ast_queue_hangup_with_cause(p->owner, cause);
		}
	} else {
		/* Owner side is hanging up. */
		p->owner = NULL;
		if (p->chan) {
			if (cause == AST_CAUSE_ANSWERED_ELSEWHERE) {
				ast_channel_hangupcause_set(p->chan, AST_CAUSE_ANSWERED_ELSEWHERE);
				ast_debug(2, "%s has AST_CAUSE_ANSWERED_ELSEWHERE set.\n",
					ast_channel_name(p->chan));
			}
			if (!ast_test_flag(p, AST_UNREAL_CARETAKER_THREAD)) {
				/*
				 * Need to actually hangup p->chan since nothing else is taking
				 * care of it.
				 */
				hangup_chan = 1;
			} else {
				ast_queue_hangup_with_cause(p->chan, cause);
			}
		}
	}

	/* this is one of our locked channels, doesn't matter which */
	ast_channel_tech_pvt_set(ast, NULL);
	ao2_ref(p, -1);

unreal_hangup_cleanup:
	ao2_unlock(p);
	if (owner) {
		ast_channel_unlock(owner);
		ast_channel_unref(owner);
	}
	if (chan) {
		ast_channel_unlock(chan);
		if (hangup_chan) {
			ast_hangup(chan);
		}
		ast_channel_unref(chan);
	}

	/* leave with the channel locked that came in */
	ast_channel_lock(ast);

	return res;
}

void ast_unreal_destructor(void *vdoomed)
{
	struct ast_unreal_pvt *doomed = vdoomed;

	ao2_cleanup(doomed->reqcap);
	doomed->reqcap = NULL;
}

struct ast_unreal_pvt *ast_unreal_alloc(size_t size, ao2_destructor_fn destructor, struct ast_format_cap *cap)
{
	struct ast_unreal_pvt *unreal;

	static const struct ast_jb_conf jb_conf = {
		.flags = 0,
		.max_size = -1,
		.resync_threshold = -1,
		.impl = "",
		.target_extra = -1,
	};

	unreal = ao2_alloc(size, destructor);
	if (!unreal) {
		return NULL;
	}

	unreal->reqcap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!unreal->reqcap) {
		ao2_ref(unreal, -1);
		return NULL;
	}
	ast_format_cap_append_from_cap(unreal->reqcap, cap, AST_MEDIA_TYPE_UNKNOWN);

	memcpy(&unreal->jb_conf, &jb_conf, sizeof(unreal->jb_conf));

	return unreal;
}

struct ast_channel *ast_unreal_new_channels(struct ast_unreal_pvt *p,
	const struct ast_channel_tech *tech, int semi1_state, int semi2_state,
	const char *exten, const char *context, const struct ast_assigned_ids *assignedids,
	const struct ast_channel *requestor, struct ast_callid *callid)
{
	struct ast_channel *owner;
	struct ast_channel *chan;
	RAII_VAR(struct ast_format *, fmt, NULL, ao2_cleanup);
	struct ast_assigned_ids id1 = {NULL, NULL};
	struct ast_assigned_ids id2 = {NULL, NULL};
	int generated_seqno = ast_atomic_fetchadd_int((int *) &name_sequence, +1);

	/* set unique ids for the two channels */
	if (assignedids && !ast_strlen_zero(assignedids->uniqueid)) {
		id1.uniqueid = assignedids->uniqueid;
		id2.uniqueid = assignedids->uniqueid2;
	}

	/* if id1 given but not id2, use default of id1;2 */
	if (id1.uniqueid && ast_strlen_zero(id2.uniqueid)) {
		char *uniqueid2;

		uniqueid2 = ast_alloca(strlen(id1.uniqueid) + 3);
		strcpy(uniqueid2, id1.uniqueid);/* Safe */
		strcat(uniqueid2, ";2");/* Safe */
		id2.uniqueid = uniqueid2;
	}

	/*
	 * Allocate two new Asterisk channels
	 *
	 * Make sure that the ;2 channel gets the same linkedid as ;1.
	 * You can't pass linkedid to both allocations since if linkedid
	 * isn't set, then each channel will generate its own linkedid.
	 */
	owner = ast_channel_alloc(1, semi1_state, NULL, NULL, NULL,
		exten, context, &id1, requestor, 0,
		"%s/%s-%08x;1", tech->type, p->name, (unsigned)generated_seqno);
	if (!owner) {
		ast_log(LOG_WARNING, "Unable to allocate owner channel structure\n");
		return NULL;
	}

	if (callid) {
		ast_channel_callid_set(owner, callid);
	}

	ast_channel_tech_set(owner, tech);
	ao2_ref(p, +1);
	ast_channel_tech_pvt_set(owner, p);

	ast_channel_nativeformats_set(owner, p->reqcap);

	/* Determine our read/write format and set it on each channel */
	fmt = ast_format_cap_get_format(p->reqcap, 0);
	if (!fmt) {
		ast_channel_tech_pvt_set(owner, NULL);
		ao2_ref(p, -1);
		ast_channel_unlock(owner);
		ast_channel_release(owner);
		return NULL;
	}

	ast_channel_set_writeformat(owner, fmt);
	ast_channel_set_rawwriteformat(owner, fmt);
	ast_channel_set_readformat(owner, fmt);
	ast_channel_set_rawreadformat(owner, fmt);

	ast_set_flag(ast_channel_flags(owner), AST_FLAG_DISABLE_DEVSTATE_CACHE);

	ast_jb_configure(owner, &p->jb_conf);

	if (ast_channel_cc_params_init(owner, requestor
		? ast_channel_get_cc_config_params((struct ast_channel *) requestor) : NULL)) {
		ast_channel_tech_pvt_set(owner, NULL);
		ao2_ref(p, -1);
		ast_channel_tech_pvt_set(owner, NULL);
		ast_channel_unlock(owner);
		ast_channel_release(owner);
		return NULL;
	}

	p->owner = owner;
	ast_channel_unlock(owner);

	chan = ast_channel_alloc(1, semi2_state, NULL, NULL, NULL,
		exten, context, &id2, owner, 0,
		"%s/%s-%08x;2", tech->type, p->name, (unsigned)generated_seqno);
	if (!chan) {
		ast_log(LOG_WARNING, "Unable to allocate chan channel structure\n");
		ast_channel_tech_pvt_set(owner, NULL);
		ao2_ref(p, -1);
		ast_channel_tech_pvt_set(owner, NULL);
		ast_channel_release(owner);
		return NULL;
	}

	if (callid) {
		ast_channel_callid_set(chan, callid);
	}

	ast_channel_tech_set(chan, tech);
	ao2_ref(p, +1);
	ast_channel_tech_pvt_set(chan, p);

	ast_channel_nativeformats_set(chan, p->reqcap);

	/* Format was already determined when setting up owner */
	ast_channel_set_writeformat(chan, fmt);
	ast_channel_set_rawwriteformat(chan, fmt);
	ast_channel_set_readformat(chan, fmt);
	ast_channel_set_rawreadformat(chan, fmt);

	ast_set_flag(ast_channel_flags(chan), AST_FLAG_DISABLE_DEVSTATE_CACHE);

	p->chan = chan;
	ast_channel_unlock(chan);

	return owner;
}
