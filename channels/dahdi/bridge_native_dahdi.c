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
 * \brief Native DAHDI bridging support.
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

#include "../sig_analog.h"
#if defined(HAVE_PRI)
#include "../sig_pri.h"
#endif	/* defined(HAVE_PRI) */
#include "../chan_dahdi.h"

#include "bridge_native_dahdi.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_technology.h"
#include "asterisk/frame.h"
#include "asterisk/format_cache.h"

/* ------------------------------------------------------------------- */

static const struct ast_channel_tech *dahdi_tech;

struct native_pvt_chan {
	/*! Original private. */
	struct dahdi_pvt *pvt;
	/*! Original private owner. */
	struct ast_channel *owner;
	/*! Original owner index. */
	int index;
	/*! Original file descriptor 0. */
	int fd0;
	/*! Original channel state. */
	int state;
	/*! Original inthreeway. */
	unsigned int inthreeway:1;
};

struct native_pvt_bridge {
	/*! Master channel in the native bridge. */
	struct dahdi_pvt *master;
	/*! Slave channel in the native bridge. */
	struct dahdi_pvt *slave;
	/*! TRUE if the bridge can start when ready. */
	unsigned int saw_start:1;
	/*! TRUE if the channels are connected in a conference. */
	unsigned int connected:1;
#if defined(HAVE_PRI) && defined(PRI_2BCT)
	/*!
	 * \brief TRUE if tried to eliminate possible PRI tromboned call.
	 *
	 * \note A tromboned call uses two B channels of the same ISDN
	 * span.  One leg comes into Asterisk, the other leg goes out of
	 * Asterisk, and Asterisk is natively bridging the two legs.
	 */
	unsigned int tried_trombone_removal:1;
#endif	/* defined(HAVE_PRI) && defined(PRI_2BCT) */
};

/*!
 * \internal
 * \brief Create a bridge technology instance for a bridge.
 * \since 12.0.0
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * \note On entry, bridge may or may not already be locked.
 * However, it can be accessed as if it were locked.
 */
static int native_bridge_create(struct ast_bridge *bridge)
{
	struct native_pvt_bridge *tech_pvt;

	ast_assert(!bridge->tech_pvt);

	tech_pvt = ast_calloc(1, sizeof(*tech_pvt));
	if (!tech_pvt) {
		return -1;
	}

	bridge->tech_pvt = tech_pvt;
	return 0;
}

/*!
 * \internal
 * \brief Destroy a bridging technology instance for a bridge.
 * \since 12.0.0
 *
 * \note On entry, bridge must NOT be locked.
 */
static void native_bridge_destroy(struct ast_bridge *bridge)
{
	struct native_pvt_bridge *tech_pvt;

	tech_pvt = bridge->tech_pvt;
	bridge->tech_pvt = NULL;
	ast_free(tech_pvt);
}

/*!
 * \internal
 * \brief Stop native bridging activity.
 * \since 12.0.0
 *
 * \param bridge What to operate upon.
 *
 * \return Nothing
 *
 * \note On entry, bridge is already locked.
 */
static void native_stop(struct ast_bridge *bridge)
{
	struct native_pvt_bridge *bridge_tech_pvt;
	struct ast_bridge_channel *cur;

	ast_assert(bridge->tech_pvt != NULL);

	AST_LIST_TRAVERSE(&bridge->channels, cur, entry) {
		struct native_pvt_chan *chan_tech_pvt;

		chan_tech_pvt = cur->tech_pvt;
		if (!chan_tech_pvt) {
			continue;
		}

		ast_mutex_lock(&chan_tech_pvt->pvt->lock);
		if (chan_tech_pvt->pvt == ast_channel_tech_pvt(cur->chan)) {
			dahdi_ec_enable(chan_tech_pvt->pvt);
		}
		if (chan_tech_pvt->index == SUB_REAL) {
			dahdi_dtmf_detect_enable(chan_tech_pvt->pvt);
		}
		ast_mutex_unlock(&chan_tech_pvt->pvt->lock);
	}

	bridge_tech_pvt = bridge->tech_pvt;
	dahdi_master_slave_unlink(bridge_tech_pvt->slave, bridge_tech_pvt->master, 1);

	ast_debug(2, "Stop native bridging %s and %s\n",
		ast_channel_name(AST_LIST_FIRST(&bridge->channels)->chan),
		ast_channel_name(AST_LIST_LAST(&bridge->channels)->chan));
}

/*!
 * \internal
 * \brief Request to stop native bridging activity.
 * \since 12.0.0
 *
 * \param bridge What to operate upon.
 *
 * \return Nothing
 *
 * \note On entry, bridge is already locked.
 */
static void native_request_stop(struct ast_bridge *bridge)
{
	struct native_pvt_bridge *tech_pvt;

	ast_assert(bridge->tech_pvt != NULL);

	tech_pvt = bridge->tech_pvt;
	if (!tech_pvt->connected) {
		return;
	}
	tech_pvt->connected = 0;

	/* Now to actually stop the bridge. */
	native_stop(bridge);
}

/*!
 * \internal
 * \brief Start native bridging activity.
 * \since 12.0.0
 *
 * \param bridge What to operate upon.
 *
 * \retval 0 on success.
 * \retval -1 on error.  Could not start the bridge.
 *
 * \note On entry, bridge may or may not already be locked.
 * However, it can be accessed as if it were locked.
 */
static int native_start(struct ast_bridge *bridge)
{
	struct native_pvt_bridge *tech_pvt;
	struct ast_bridge_channel *bc0;
	struct ast_bridge_channel *bc1;
	struct native_pvt_chan *npc0;
	struct native_pvt_chan *npc1;
	struct ast_channel *c0;
	struct ast_channel *c1;
	struct dahdi_pvt *p0;
	struct dahdi_pvt *p1;
	struct dahdi_pvt *master;
	struct dahdi_pvt *slave;
	int inconf;
	int nothing_ok;

	ast_assert(bridge->tech_pvt != NULL);

	bc0 = AST_LIST_FIRST(&bridge->channels);
	bc1 = AST_LIST_LAST(&bridge->channels);
	c0 = bc0->chan;
	c1 = bc1->chan;

	/* Lock channels and privates */
	for (;;) {
		ast_channel_lock(c0);
		if (!ast_channel_trylock(c1)) {
			p0 = ast_channel_tech_pvt(c0);
			if (!ast_mutex_trylock(&p0->lock)) {
				p1 = ast_channel_tech_pvt(c1);
				if (!ast_mutex_trylock(&p1->lock)) {
					/* Got all locks */
					break;
				}
				ast_mutex_unlock(&p0->lock);
			}
			ast_channel_unlock(c1);
		}
		ast_channel_unlock(c0);
		sched_yield();
	}

	npc0 = bc0->tech_pvt;
	ast_assert(npc0 != NULL);
	npc0->pvt = p0;
	npc0->owner = p0->owner;
	npc0->index = dahdi_get_index(c0, p0, 0);
	npc0->fd0 = ast_channel_fd(c0, 0);
	npc0->state = -1;
	npc0->inthreeway = p0->subs[SUB_REAL].inthreeway;

	npc1 = bc1->tech_pvt;
	ast_assert(npc1 != NULL);
	npc1->pvt = p1;
	npc1->owner = p1->owner;
	npc1->index = dahdi_get_index(c1, p1, 0);
	npc1->fd0 = ast_channel_fd(c1, 0);
	npc1->state = -1;
	npc1->inthreeway = p1->subs[SUB_REAL].inthreeway;

	/*
	 * Check things that can change on the privates while in native
	 * bridging and cause native to not activate.
	 */
	if (npc0->index < 0 || npc1->index < 0
#if defined(HAVE_PRI)
		/*
		 * PRI nobch channels (hold and call waiting) are equivalent to
		 * pseudo channels and cannot be nativly bridged.
		 */
		|| (dahdi_sig_pri_lib_handles(p0->sig)
			&& ((struct sig_pri_chan *) p0->sig_pvt)->no_b_channel)
		|| (dahdi_sig_pri_lib_handles(p1->sig)
			&& ((struct sig_pri_chan *) p1->sig_pvt)->no_b_channel)
#endif	/* defined(HAVE_PRI) */
		) {
		ast_mutex_unlock(&p0->lock);
		ast_mutex_unlock(&p1->lock);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return -1;
	}

	inconf = 0;
	nothing_ok = 1;
	master = NULL;
	slave = NULL;
	if (npc0->index == SUB_REAL && npc1->index == SUB_REAL) {
		if (p0->owner && p1->owner) {
			/*
			 * If we don't have a call-wait in a 3-way, and we aren't in a
			 * 3-way, we can be master.
			 */
			if (!p0->subs[SUB_CALLWAIT].inthreeway && !p1->subs[SUB_REAL].inthreeway) {
				master = p0;
				slave = p1;
				inconf = 1;
			} else if (!p1->subs[SUB_CALLWAIT].inthreeway && !p0->subs[SUB_REAL].inthreeway) {
				master = p1;
				slave = p0;
				inconf = 1;
			} else {
				ast_log(LOG_WARNING, "Huh?  Both calls are callwaits or 3-ways?  That's clever...?\n");
				ast_log(LOG_WARNING, "p0: chan %d/%d/CW%d/3W%d, p1: chan %d/%d/CW%d/3W%d\n",
					p0->channel,
					npc0->index, (p0->subs[SUB_CALLWAIT].dfd > -1) ? 1 : 0,
					p0->subs[SUB_REAL].inthreeway,
					p0->channel,
					npc0->index, (p1->subs[SUB_CALLWAIT].dfd > -1) ? 1 : 0,
					p1->subs[SUB_REAL].inthreeway);
			}
			nothing_ok = 0;
		}
	} else if (npc0->index == SUB_REAL && npc1->index == SUB_THREEWAY) {
		if (p1->subs[SUB_THREEWAY].inthreeway) {
			master = p1;
			slave = p0;
			nothing_ok = 0;
		}
	} else if (npc0->index == SUB_THREEWAY && npc1->index == SUB_REAL) {
		if (p0->subs[SUB_THREEWAY].inthreeway) {
			master = p0;
			slave = p1;
			nothing_ok = 0;
		}
	} else if (npc0->index == SUB_REAL && npc1->index == SUB_CALLWAIT) {
		/*
		 * We have a real and a call wait.  If we're in a three way
		 * call, put us in it, otherwise, don't put us in anything.
		 */
		if (p1->subs[SUB_CALLWAIT].inthreeway) {
			master = p1;
			slave = p0;
			nothing_ok = 0;
		}
	} else if (npc0->index == SUB_CALLWAIT && npc1->index == SUB_REAL) {
		/* Same as previous */
		if (p0->subs[SUB_CALLWAIT].inthreeway) {
			master = p0;
			slave = p1;
			nothing_ok = 0;
		}
	}
	ast_debug(3, "master: %d, slave: %d, nothing_ok: %d\n",
		master ? master->channel : 0,
		slave ? slave->channel : 0,
		nothing_ok);
	if (master && slave) {
		/*
		 * Stop any tones, or play ringtone as appropriate.  If they are
		 * bridged in an active threeway call with a channel that is
		 * ringing, we should indicate ringing.
		 */
		if (npc1->index == SUB_THREEWAY
			&& p1->subs[SUB_THREEWAY].inthreeway
			&& p1->subs[SUB_REAL].owner
			&& p1->subs[SUB_REAL].inthreeway
			&& ast_channel_state(p1->subs[SUB_REAL].owner) == AST_STATE_RINGING) {
			ast_debug(2,
				"Playing ringback on %d/%d(%s) since %d/%d(%s) is in a ringing three-way\n",
				p0->channel, npc0->index, ast_channel_name(c0),
				p1->channel, npc1->index, ast_channel_name(c1));
			tone_zone_play_tone(p0->subs[npc0->index].dfd, DAHDI_TONE_RINGTONE);
			npc1->state = ast_channel_state(p1->subs[SUB_REAL].owner);
		} else {
			ast_debug(2, "Stopping tones on %d/%d(%s) talking to %d/%d(%s)\n",
				p0->channel, npc0->index, ast_channel_name(c0),
				p1->channel, npc1->index, ast_channel_name(c1));
			tone_zone_play_tone(p0->subs[npc0->index].dfd, -1);
		}

		if (npc0->index == SUB_THREEWAY
			&& p0->subs[SUB_THREEWAY].inthreeway
			&& p0->subs[SUB_REAL].owner
			&& p0->subs[SUB_REAL].inthreeway
			&& ast_channel_state(p0->subs[SUB_REAL].owner) == AST_STATE_RINGING) {
			ast_debug(2,
				"Playing ringback on %d/%d(%s) since %d/%d(%s) is in a ringing three-way\n",
				p1->channel, npc1->index, ast_channel_name(c1),
				p0->channel, npc0->index, ast_channel_name(c0));
			tone_zone_play_tone(p1->subs[npc1->index].dfd, DAHDI_TONE_RINGTONE);
			npc0->state = ast_channel_state(p0->subs[SUB_REAL].owner);
		} else {
			ast_debug(2, "Stopping tones on %d/%d(%s) talking to %d/%d(%s)\n",
				p1->channel, npc1->index, ast_channel_name(c1),
				p0->channel, npc0->index, ast_channel_name(c0));
			tone_zone_play_tone(p1->subs[npc1->index].dfd, -1);
		}

		if (npc0->index == SUB_REAL && npc1->index == SUB_REAL) {
			if (!p0->echocanbridged || !p1->echocanbridged) {
				/* Disable echo cancellation if appropriate */
				dahdi_ec_disable(p0);
				dahdi_ec_disable(p1);
			}
		}
		dahdi_master_slave_link(slave, master);
		master->inconference = inconf;
	} else if (!nothing_ok) {
		ast_log(LOG_WARNING, "Can't link %d/%s with %d/%s\n",
			p0->channel, subnames[npc0->index],
			p1->channel, subnames[npc1->index]);
	}
	dahdi_conf_update(p0);
	dahdi_conf_update(p1);

	ast_channel_unlock(c0);
	ast_channel_unlock(c1);

	/* Native bridge failed */
	if ((!master || !slave) && !nothing_ok) {
		ast_mutex_unlock(&p0->lock);
		ast_mutex_unlock(&p1->lock);
		return -1;
	}

	if (npc0->index == SUB_REAL) {
		dahdi_dtmf_detect_disable(p0);
	}
	if (npc1->index == SUB_REAL) {
		dahdi_dtmf_detect_disable(p1);
	}

	ast_mutex_unlock(&p0->lock);
	ast_mutex_unlock(&p1->lock);

	tech_pvt = bridge->tech_pvt;
	tech_pvt->master = master;
	tech_pvt->slave = slave;

	ast_debug(2, "Start native bridging %s and %s\n",
		ast_channel_name(c0), ast_channel_name(c1));

#if defined(HAVE_PRI) && defined(PRI_2BCT)
	if (!tech_pvt->tried_trombone_removal) {
		tech_pvt->tried_trombone_removal = 1;

		if (p0->pri && p0->pri == p1->pri && p0->pri->transfer) {
			q931_call *q931_c0;
			q931_call *q931_c1;

			/* Try to eliminate the tromboned call. */
			ast_mutex_lock(&p0->pri->lock);
			ast_assert(dahdi_sig_pri_lib_handles(p0->sig));
			ast_assert(dahdi_sig_pri_lib_handles(p1->sig));
			q931_c0 = ((struct sig_pri_chan *) (p0->sig_pvt))->call;
			q931_c1 = ((struct sig_pri_chan *) (p1->sig_pvt))->call;
			if (q931_c0 && q931_c1) {
				pri_channel_bridge(q931_c0, q931_c1);
				ast_debug(2, "Attempt to eliminate tromboned call with %s and %s\n",
					ast_channel_name(c0), ast_channel_name(c1));
			}
			ast_mutex_unlock(&p0->pri->lock);
		}
	}
#endif	/* defined(HAVE_PRI) && defined(PRI_2BCT) */
	return 0;
}

/*!
 * \internal
 * \brief Request to start native bridging activity.
 * \since 12.0.0
 *
 * \param bridge What to operate upon.
 *
 * \return Nothing
 *
 * \note On entry, bridge may or may not already be locked.
 * However, it can be accessed as if it were locked.
 */
static void native_request_start(struct ast_bridge *bridge)
{
	struct native_pvt_bridge *tech_pvt;
	struct ast_bridge_channel *cur;

	ast_assert(bridge->tech_pvt != NULL);

	tech_pvt = bridge->tech_pvt;

	if (bridge->num_channels != 2 || !tech_pvt->saw_start || tech_pvt->connected) {
		return;
	}
	AST_LIST_TRAVERSE(&bridge->channels, cur, entry) {
		if (cur->suspended || !cur->tech_pvt) {
			return;
		}
	}

	/* Actually try starting the native bridge. */
	if (native_start(bridge)) {
		return;
	}
	tech_pvt->connected = 1;
}

/*!
 * \internal
 * \brief Request a bridge technology instance start operations.
 * \since 12.0.0
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * \note On entry, bridge may or may not already be locked.
 * However, it can be accessed as if it were locked.
 */
static int native_bridge_start(struct ast_bridge *bridge)
{
	struct native_pvt_bridge *tech_pvt;

	ast_assert(bridge->tech_pvt != NULL);

	tech_pvt = bridge->tech_pvt;
	tech_pvt->saw_start = 1;

	native_request_start(bridge);
	return 0;
}

/*!
 * \internal
 * \brief Request a bridge technology instance stop in preparation for being destroyed.
 * \since 12.0.0
 *
 * \note On entry, bridge is already locked.
 */
static void native_bridge_stop(struct ast_bridge *bridge)
{
	struct native_pvt_bridge *tech_pvt;

	tech_pvt = bridge->tech_pvt;
	if (!tech_pvt) {
		return;
	}

	tech_pvt->saw_start = 0;
	native_request_stop(bridge);
}

/*!
 * \internal
 * \brief Add a channel to a bridging technology instance for a bridge.
 * \since 12.0.0
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * \note On entry, bridge is already locked.
 */
static int native_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct native_pvt_chan *tech_pvt;
	struct ast_channel *c0;
	struct ast_channel *c1;

	ast_assert(!bridge_channel->tech_pvt);

	tech_pvt = ast_calloc(1, sizeof(*tech_pvt));
	if (!tech_pvt) {
		return -1;
	}

	bridge_channel->tech_pvt = tech_pvt;
	native_request_start(bridge);

	/*
	 * Make the channels compatible in case the native bridge did
	 * not start for some reason and we need to fallback to 1-1
	 * bridging.
	 */
	c0 = AST_LIST_FIRST(&bridge->channels)->chan;
	c1 = AST_LIST_LAST(&bridge->channels)->chan;
	if (c0 == c1) {
		return 0;
	}
	return ast_channel_make_compatible(c0, c1);
}

/*!
 * \internal
 * \brief Remove a channel from a bridging technology instance for a bridge.
 * \since 12.0.0
 *
 * \note On entry, bridge is already locked.
 */
static void native_bridge_leave(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct native_pvt_chan *tech_pvt;

	native_request_stop(bridge);

	tech_pvt = bridge_channel->tech_pvt;
	bridge_channel->tech_pvt = NULL;
	ast_free(tech_pvt);
}

/*!
 * \internal
 * \brief Suspend a channel on a bridging technology instance for a bridge.
 * \since 12.0.0
 *
 * \note On entry, bridge is already locked.
 */
static void native_bridge_suspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	native_request_stop(bridge);
}

/*!
 * \internal
 * \brief Unsuspend a channel on a bridging technology instance for a bridge.
 * \since 12.0.0
 *
 * \note On entry, bridge is already locked.
 */
static void native_bridge_unsuspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	native_request_start(bridge);
}

/*!
 * \internal
 * \brief Check if channel is compatible.
 * \since 12.0.0
 *
 * \param bridge_channel Is this channel compatible.
 *
 * \retval TRUE if channel is compatible with native DAHDI bridge.
 */
static int native_bridge_is_capable(struct ast_bridge_channel *bridge_channel)
{
	struct ast_channel *chan = bridge_channel->chan;
	struct dahdi_pvt *pvt;
	int is_capable;

	if (ao2_container_count(bridge_channel->features->dtmf_hooks)) {
		ast_debug(2, "Channel '%s' has DTMF hooks.\n", ast_channel_name(chan));
		return 0;
	}

	ast_channel_lock(chan);

	if (dahdi_tech != ast_channel_tech(chan)) {
		ast_debug(2, "Channel '%s' is not %s.\n",
			ast_channel_name(chan), dahdi_tech->type);
		ast_channel_unlock(chan);
		return 0;
	}
	if (ast_channel_has_audio_frame_or_monitor(chan)) {
		ast_debug(2, "Channel '%s' has an active monitor, audiohook, or framehook.\n",
			ast_channel_name(chan));
		ast_channel_unlock(chan);
		return 0;
	}
	pvt = ast_channel_tech_pvt(chan);
	if (!pvt || !pvt->sig) {
		/* No private; or signaling is for a pseudo channel. */
		ast_channel_unlock(chan);
		return 0;
	}

	is_capable = 1;
	ast_mutex_lock(&pvt->lock);

	if (pvt->callwaiting && pvt->callwaitingcallerid) {
		/*
		 * Call Waiting Caller ID requires DTMF detection to know if it
		 * can send the CID spill.
		 */
		ast_debug(2, "Channel '%s' has call waiting caller ID enabled.\n",
			ast_channel_name(chan));
		is_capable = 0;
	}

	ast_mutex_unlock(&pvt->lock);
	ast_channel_unlock(chan);

	return is_capable;
}

/*!
 * \internal
 * \brief Check if a bridge is compatible with the bridging technology.
 * \since 12.0.0
 *
 * \retval 0 if not compatible
 * \retval non-zero if compatible
 *
 * \note On entry, bridge may or may not already be locked.
 * However, it can be accessed as if it were locked.
 */
static int native_bridge_compatible(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *cur;

	/* We require two channels before even considering native bridging. */
	if (bridge->num_channels != 2) {
		ast_debug(1, "Bridge %s: Cannot use native DAHDI.  Must have two channels.\n",
			bridge->uniqueid);
		return 0;
	}

	AST_LIST_TRAVERSE(&bridge->channels, cur, entry) {
		if (!native_bridge_is_capable(cur)) {
			ast_debug(1, "Bridge %s: Cannot use native DAHDI.  Channel '%s' not compatible.\n",
				bridge->uniqueid, ast_channel_name(cur->chan));
			return 0;
		}
	}

	return -1;
}

/*!
 * \internal
 * \brief Check if something changed on the channel.
 * \since 12.0.0
 *
 * \param bridge_channel What to operate upon.
 *
 * \retval 0 Nothing changed.
 * \retval -1 Something changed.
 *
 * \note On entry, bridge_channel->bridge is already locked.
 */
static int native_chan_changed(struct ast_bridge_channel *bridge_channel)
{
	struct native_pvt_chan *tech_pvt;
	struct ast_channel *chan;
	struct dahdi_pvt *pvt;
	int idx = -1;

	ast_assert(bridge_channel->tech_pvt != NULL);

	tech_pvt = bridge_channel->tech_pvt;

	chan = bridge_channel->chan;
	ast_channel_lock(chan);
	pvt = ast_channel_tech_pvt(chan);
	if (tech_pvt->pvt == pvt) {
		idx = dahdi_get_index(chan, pvt, 1);
	}
	ast_channel_unlock(chan);

	if (/* Did chan get masqueraded or PRI change associated B channel? */
		tech_pvt->pvt != pvt
		/* Did the pvt active owner change? */
		|| tech_pvt->owner != pvt->owner
		/* Did the pvt three way call status change? */
		|| tech_pvt->inthreeway != pvt->subs[SUB_REAL].inthreeway
		/* Did the owner index change? */
		|| tech_pvt->index != idx
		/*
		 * Did chan file descriptor change? (This seems redundant with
		 * masquerade and active owner change checks.)
		 */
		|| tech_pvt->fd0 != ast_channel_fd(chan, 0)
		/* Did chan state change? i.e. Did it stop ringing? */
		|| (pvt->subs[SUB_REAL].owner
			&& tech_pvt->state > -1
			&& tech_pvt->state != ast_channel_state(pvt->subs[SUB_REAL].owner))) {
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Check if something changed on the bridge channels.
 * \since 12.0.0
 *
 * \param bridge What to operate upon.
 *
 * \retval 0 Nothing changed.
 * \retval -1 Something changed.
 *
 * \note On entry, bridge is already locked.
 */
static int native_bridge_changed(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *cur;

	AST_LIST_TRAVERSE(&bridge->channels, cur, entry) {
		if (native_chan_changed(cur)) {
			ast_debug(1, "Bridge %s: Something changed on channel '%s'.\n",
				bridge->uniqueid, ast_channel_name(cur->chan));
			return -1;
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief Write a frame into the bridging technology instance for a bridge.
 * \since 12.0.0
 *
 * \note The bridge must be tolerant of bridge_channel being NULL.
 *
 * \retval 0 Frame accepted into the bridge.
 * \retval -1 Frame needs to be deferred.
 *
 * \note On entry, bridge is already locked.
 */
static int native_bridge_write(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct native_pvt_bridge *tech_pvt;

	/*
	 * When we are not native bridged by DAHDI, we are like a normal
	 * 1-1 bridge.
	 */

	ast_assert(bridge->tech_pvt != NULL);

	/* Recheck native bridging validity. */
	tech_pvt = bridge->tech_pvt;
	switch (frame->frametype) {
	case AST_FRAME_VOICE:
	case AST_FRAME_VIDEO:
		if (!tech_pvt->connected) {
			/* Don't try to start native mode on media frames. */
			break;
		}
		if (native_bridge_changed(bridge)) {
			native_request_stop(bridge);
			native_request_start(bridge);
			if (!tech_pvt->connected) {
				break;
			}
		}

		/*
		 * Native bridge handles voice frames in hardware.  However, it
		 * also passes the frames up to Asterisk anyway.  Discard the
		 * media frames.
		 */
		return 0;
	default:
		if (!tech_pvt->connected) {
			native_request_start(bridge);
			break;
		}
		if (native_bridge_changed(bridge)) {
			native_request_stop(bridge);
			native_request_start(bridge);
		}
		break;
	}

	return ast_bridge_queue_everyone_else(bridge, bridge_channel, frame);
}

static struct ast_bridge_technology native_bridge = {
	.name = "native_dahdi",
	.capabilities = AST_BRIDGE_CAPABILITY_NATIVE,
	.preference = AST_BRIDGE_PREFERENCE_BASE_NATIVE,
	.create = native_bridge_create,
	.start = native_bridge_start,
	.stop = native_bridge_stop,
	.destroy = native_bridge_destroy,
	.join = native_bridge_join,
	.leave = native_bridge_leave,
	.suspend = native_bridge_suspend,
	.unsuspend = native_bridge_unsuspend,
	.compatible = native_bridge_compatible,
	.write = native_bridge_write,
};

/*!
 * \internal
 * \brief Destroy the DAHDI native bridge support.
 * \since 12.0.0
 *
 * \return Nothing
 */
void dahdi_native_unload(void)
{
	ast_bridge_technology_unregister(&native_bridge);
}

/*!
 * \internal
 * \brief Initialize the DAHDI native bridge support.
 * \since 12.0.0
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int dahdi_native_load(struct ast_module *mod, const struct ast_channel_tech *tech)
{
	dahdi_tech = tech;

	if (__ast_bridge_technology_register(&native_bridge, mod)) {
		dahdi_native_unload();
		return -1;
	}

	return 0;
}
