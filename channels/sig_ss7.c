/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010 Digium, Inc.
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
 * \brief SS7 signaling module.
 *
 * \author Matthew Fredrickson <creslin@digium.com>
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#if defined(HAVE_SS7)

#include <signal.h>

#include "asterisk/pbx.h"
#include "asterisk/causes.h"
#include "asterisk/musiconhold.h"
#include "asterisk/cli.h"
#include "asterisk/callerid.h"
#include "asterisk/transcap.h"
#include "asterisk/stasis_channels.h"

#include "sig_ss7.h"
#if !defined(LIBSS7_ABI_COMPATIBILITY)
#error "Upgrade your libss7"
#elif LIBSS7_ABI_COMPATIBILITY != 2
#error "Your installed libss7 is not compatible"
#endif

/* ------------------------------------------------------------------- */

static const char *sig_ss7_call_level2str(enum sig_ss7_call_level level)
{
	switch (level) {
	case SIG_SS7_CALL_LEVEL_IDLE:
		return "Idle";
	case SIG_SS7_CALL_LEVEL_ALLOCATED:
		return "Allocated";
	case SIG_SS7_CALL_LEVEL_CONTINUITY:
		return "Continuity";
	case SIG_SS7_CALL_LEVEL_SETUP:
		return "Setup";
	case SIG_SS7_CALL_LEVEL_PROCEEDING:
		return "Proceeding";
	case SIG_SS7_CALL_LEVEL_ALERTING:
		return "Alerting";
	case SIG_SS7_CALL_LEVEL_CONNECT:
		return "Connect";
	}
	return "Unknown";
}

static void sig_ss7_unlock_private(struct sig_ss7_chan *p)
{
	if (sig_ss7_callbacks.unlock_private) {
		sig_ss7_callbacks.unlock_private(p->chan_pvt);
	}
}

static void sig_ss7_lock_private(struct sig_ss7_chan *p)
{
	if (sig_ss7_callbacks.lock_private) {
		sig_ss7_callbacks.lock_private(p->chan_pvt);
	}
}

static void sig_ss7_deadlock_avoidance_private(struct sig_ss7_chan *p)
{
	if (sig_ss7_callbacks.deadlock_avoidance_private) {
		sig_ss7_callbacks.deadlock_avoidance_private(p->chan_pvt);
	} else {
		/* Fallback to the old way if callback not present. */
		sig_ss7_unlock_private(p);
		sched_yield();
		sig_ss7_lock_private(p);
	}
}

void sig_ss7_set_alarm(struct sig_ss7_chan *p, int in_alarm)
{
	p->inalarm = in_alarm;
	if (sig_ss7_callbacks.set_alarm) {
		sig_ss7_callbacks.set_alarm(p->chan_pvt, in_alarm);
	}
}

static void sig_ss7_set_dialing(struct sig_ss7_chan *p, int is_dialing)
{
	if (sig_ss7_callbacks.set_dialing) {
		sig_ss7_callbacks.set_dialing(p->chan_pvt, is_dialing);
	}
}

static void sig_ss7_set_digital(struct sig_ss7_chan *p, int is_digital)
{
	if (sig_ss7_callbacks.set_digital) {
		sig_ss7_callbacks.set_digital(p->chan_pvt, is_digital);
	}
}

static void sig_ss7_set_outgoing(struct sig_ss7_chan *p, int is_outgoing)
{
	p->outgoing = is_outgoing;
	if (sig_ss7_callbacks.set_outgoing) {
		sig_ss7_callbacks.set_outgoing(p->chan_pvt, is_outgoing);
	}
}

static void sig_ss7_set_inservice(struct sig_ss7_chan *p, int is_inservice)
{
	p->inservice = is_inservice;
	if (sig_ss7_callbacks.set_inservice) {
		sig_ss7_callbacks.set_inservice(p->chan_pvt, is_inservice);
	}
}

static void sig_ss7_set_locallyblocked(struct sig_ss7_chan *p, int is_blocked, int type)
{
	if (is_blocked) {
		p->locallyblocked |= type;
	} else {
		p->locallyblocked &= ~type;
	}

	if (sig_ss7_callbacks.set_locallyblocked) {
		sig_ss7_callbacks.set_locallyblocked(p->chan_pvt, p->locallyblocked);
	}
}

static void sig_ss7_set_remotelyblocked(struct sig_ss7_chan *p, int is_blocked, int type)
{
	if (is_blocked) {
		p->remotelyblocked |= type;
	} else {
		p->remotelyblocked &= ~type;
	}

	if (sig_ss7_callbacks.set_remotelyblocked) {
		sig_ss7_callbacks.set_remotelyblocked(p->chan_pvt, p->remotelyblocked);
	}
}

/*!
 * \internal
 * \brief Open the SS7 channel media path.
 * \since 1.8.12
 *
 * \param p Channel private control structure.
 *
 * \return Nothing
 */
static void sig_ss7_open_media(struct sig_ss7_chan *p)
{
	if (sig_ss7_callbacks.open_media) {
		sig_ss7_callbacks.open_media(p->chan_pvt);
	}
}

/*!
 * \internal
 * \brief Set the caller id information in the parent module.
 * \since 1.8
 *
 * \param p sig_ss7 channel structure.
 *
 * \return Nothing
 */
static void sig_ss7_set_caller_id(struct sig_ss7_chan *p)
{
	struct ast_party_caller caller;

	if (sig_ss7_callbacks.set_callerid) {
		ast_party_caller_init(&caller);

		caller.id.name.str = p->cid_name;
		caller.id.name.presentation = p->callingpres;
		caller.id.name.valid = 1;

		caller.id.number.str = p->cid_num;
		caller.id.number.plan = p->cid_ton;
		caller.id.number.presentation = p->callingpres;
		caller.id.number.valid = 1;

		if (!ast_strlen_zero(p->cid_subaddr)) {
			caller.id.subaddress.valid = 1;
			//caller.id.subaddress.type = 0;/* nsap */
			//caller.id.subaddress.odd_even_indicator = 0;
			caller.id.subaddress.str = p->cid_subaddr;
		}

		caller.ani.number.str = p->cid_ani;
		//caller.ani.number.plan = p->xxx;
		//caller.ani.number.presentation = p->xxx;
		caller.ani.number.valid = 1;

		caller.ani2 = p->cid_ani2;
		sig_ss7_callbacks.set_callerid(p->chan_pvt, &caller);
	}
}

/*!
 * \internal
 * \brief Set the Dialed Number Identifier.
 * \since 1.8
 *
 * \param p sig_ss7 channel structure.
 * \param dnid Dialed Number Identifier string.
 *
 * \return Nothing
 */
static void sig_ss7_set_dnid(struct sig_ss7_chan *p, const char *dnid)
{
	if (sig_ss7_callbacks.set_dnid) {
		sig_ss7_callbacks.set_dnid(p->chan_pvt, dnid);
	}
}

static int sig_ss7_play_tone(struct sig_ss7_chan *p, enum sig_ss7_tone tone)
{
	int res;

	if (sig_ss7_callbacks.play_tone) {
		res = sig_ss7_callbacks.play_tone(p->chan_pvt, tone);
	} else {
		res = -1;
	}
	return res;
}

static int sig_ss7_set_echocanceller(struct sig_ss7_chan *p, int enable)
{
	if (sig_ss7_callbacks.set_echocanceller) {
		return sig_ss7_callbacks.set_echocanceller(p->chan_pvt, enable);
	}
	return -1;
}

static void sig_ss7_loopback(struct sig_ss7_chan *p, int enable)
{
	if (p->loopedback != enable) {
		p->loopedback = enable;
		if (sig_ss7_callbacks.set_loopback) {
			sig_ss7_callbacks.set_loopback(p->chan_pvt, enable);
		}
	}
}

static struct ast_channel *sig_ss7_new_ast_channel(struct sig_ss7_chan *p, int state,
	int ulaw, int transfercapability, char *exten,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
{
	struct ast_channel *ast;

	if (sig_ss7_callbacks.new_ast_channel) {
		ast = sig_ss7_callbacks.new_ast_channel(p->chan_pvt, state, ulaw, exten,
			assignedids, requestor);
	} else {
		return NULL;
	}
	if (!ast) {
		return NULL;
	}

	if (!p->owner) {
		p->owner = ast;
	}

	if (p->outgoing) {
		p->do_hangup = SS7_HANGUP_FREE_CALL;
	} else {
		p->do_hangup = SS7_HANGUP_SEND_REL;
	}

	ast_channel_transfercapability_set(ast, transfercapability);
	pbx_builtin_setvar_helper(ast, "TRANSFERCAPABILITY",
		ast_transfercapability2str(transfercapability));
	if (transfercapability & AST_TRANS_CAP_DIGITAL) {
		sig_ss7_set_digital(p, 1);
	}

	return ast;
}

static void sig_ss7_handle_link_exception(struct sig_ss7_linkset *linkset, int which)
{
	if (sig_ss7_callbacks.handle_link_exception) {
		sig_ss7_callbacks.handle_link_exception(linkset, which);
	}
}

static struct sig_ss7_linkset *sig_ss7_find_linkset(struct ss7 *ss7)
{
	if (sig_ss7_callbacks.find_linkset) {
		return sig_ss7_callbacks.find_linkset(ss7);
	}
	return NULL;
}

/*!
 * \internal
 * \brief Determine if a private channel structure is available.
 *
 * \param pvt Channel to determine if available.
 *
 * \return TRUE if the channel is available.
 */
static int sig_ss7_is_chan_available(struct sig_ss7_chan *pvt)
{
	if (pvt->inservice && !pvt->inalarm && !pvt->owner && !pvt->ss7call
		&& pvt->call_level == SIG_SS7_CALL_LEVEL_IDLE
		&& !pvt->locallyblocked && !pvt->remotelyblocked) {
		return 1;
	}
	return 0;
}

/*!
 * \internal
 * \brief Obtain the sig_ss7 owner channel lock if the owner exists.
 * \since 1.8
 *
 * \param ss7 SS7 linkset control structure.
 * \param chanpos Channel position in the span.
 *
 * \note Assumes the ss7->lock is already obtained.
 * \note Assumes the sig_ss7_lock_private(ss7->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void sig_ss7_lock_owner(struct sig_ss7_linkset *ss7, int chanpos)
{
	for (;;) {
		if (!ss7->pvts[chanpos]->owner) {
			/* There is no owner lock to get. */
			break;
		}
		if (!ast_channel_trylock(ss7->pvts[chanpos]->owner)) {
			/* We got the lock */
			break;
		}

		/* Avoid deadlock */
		sig_ss7_unlock_private(ss7->pvts[chanpos]);
		DEADLOCK_AVOIDANCE(&ss7->lock);
		sig_ss7_lock_private(ss7->pvts[chanpos]);
	}
}

/*!
 * \internal
 * \brief Queue the given frame onto the owner channel.
 * \since 1.8
 *
 * \param ss7 SS7 linkset control structure.
 * \param chanpos Channel position in the span.
 * \param frame Frame to queue onto the owner channel.
 *
 * \note Assumes the ss7->lock is already obtained.
 * \note Assumes the sig_ss7_lock_private(ss7->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void sig_ss7_queue_frame(struct sig_ss7_linkset *ss7, int chanpos, struct ast_frame *frame)
{
	sig_ss7_lock_owner(ss7, chanpos);
	if (ss7->pvts[chanpos]->owner) {
		ast_queue_frame(ss7->pvts[chanpos]->owner, frame);
		ast_channel_unlock(ss7->pvts[chanpos]->owner);
	}
}

/*!
 * \internal
 * \brief Queue a control frame of the specified subclass onto the owner channel.
 * \since 1.8
 *
 * \param ss7 SS7 linkset control structure.
 * \param chanpos Channel position in the span.
 * \param subclass Control frame subclass to queue onto the owner channel.
 *
 * \note Assumes the ss7->lock is already obtained.
 * \note Assumes the sig_ss7_lock_private(ss7->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void sig_ss7_queue_control(struct sig_ss7_linkset *ss7, int chanpos, int subclass)
{
	struct ast_frame f = {AST_FRAME_CONTROL, };
	struct sig_ss7_chan *p = ss7->pvts[chanpos];

	if (sig_ss7_callbacks.queue_control) {
		sig_ss7_callbacks.queue_control(p->chan_pvt, subclass);
	}

	f.subclass.integer = subclass;
	sig_ss7_queue_frame(ss7, chanpos, &f);
}

/*!
 * \internal
 * \brief Queue a PVT_CAUSE_CODE frame onto the owner channel.
 * \since 11.0
 *
 * \param owner Owner channel of the pvt.
 * \param cause String describing the cause to be placed into the frame.
 *
 * \note Assumes the linkset->lock is already obtained.
 * \note Assumes the sig_ss7_lock_private(linkset->pvts[chanpos]) is already obtained.
 * \note Assumes linkset->pvts[chanpos]->owner is non-NULL and its lock is already obtained.
 *
 * \return Nothing
 */
static void ss7_queue_pvt_cause_data(struct ast_channel *owner, const char *cause, int ast_cause)
{
	struct ast_control_pvt_cause_code *cause_code;
	int datalen = sizeof(*cause_code) + strlen(cause);

	cause_code = ast_alloca(datalen);
	memset(cause_code, 0, datalen);
	cause_code->ast_cause = ast_cause;
	ast_copy_string(cause_code->chan_name, ast_channel_name(owner), AST_CHANNEL_NAME);
	ast_copy_string(cause_code->code, cause, datalen + 1 - sizeof(*cause_code));
	ast_queue_control_data(owner, AST_CONTROL_PVT_CAUSE_CODE, cause_code, datalen);
	ast_channel_hangupcause_hash_set(owner, cause_code, datalen);
}


/*!
 * \brief Find the channel position by CIC/DPC.
 *
 * \param linkset SS7 linkset control structure.
 * \param cic Circuit Identification Code
 * \param dpc Destination Point Code
 *
 * \retval chanpos on success.
 * \retval -1 on error.
 */
int sig_ss7_find_cic(struct sig_ss7_linkset *linkset, int cic, unsigned int dpc)
{
	int i;
	int winner = -1;
	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i] && (linkset->pvts[i]->dpc == dpc && linkset->pvts[i]->cic == cic)) {
			winner = i;
			break;
		}
	}
	return winner;
}

/*!
 * \internal
 * \brief Find the channel position by CIC/DPC and gripe if not found.
 *
 * \param linkset SS7 linkset control structure.
 * \param cic Circuit Identification Code
 * \param dpc Destination Point Code
 * \param msg_name Message type name that failed.
 *
 * \retval chanpos on success.
 * \retval -1 on error.
 */
static int ss7_find_cic_gripe(struct sig_ss7_linkset *linkset, int cic, unsigned int dpc, const char *msg_name)
{
	int chanpos;

	chanpos = sig_ss7_find_cic(linkset, cic, dpc);
	if (chanpos < 0) {
		ast_log(LOG_WARNING, "Linkset %d: SS7 %s requested on unconfigured CIC/DPC %d/%d.\n",
			linkset->span, msg_name, cic, dpc);
		return -1;
	}
	return chanpos;
}

static struct sig_ss7_chan *ss7_find_pvt(struct ss7 *ss7, int cic, unsigned int dpc)
{
	int chanpos;
	struct sig_ss7_linkset *winner;

	winner = sig_ss7_find_linkset(ss7);
	if (winner && (chanpos = sig_ss7_find_cic(winner, cic, dpc)) > -1) {
		return winner->pvts[chanpos];
	}
	return NULL;
}

int sig_ss7_cb_hangup(struct ss7 *ss7, int cic, unsigned int dpc, int cause, int do_hangup)
{
	struct sig_ss7_chan *p;
	int res;

	if (!(p = ss7_find_pvt(ss7, cic, dpc))) {
		return SS7_CIC_NOT_EXISTS;
	}

	sig_ss7_lock_private(p);
	if (p->owner) {
		ast_channel_hangupcause_set(p->owner, cause);
		ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
		p->do_hangup = do_hangup;
		res = SS7_CIC_USED;
	} else {
		res = SS7_CIC_IDLE;
	}
	sig_ss7_unlock_private(p);

	return res;
}

void sig_ss7_cb_call_null(struct ss7 *ss7, struct isup_call *call, int lock)
{
	int i;
	struct sig_ss7_linkset *winner;

	winner = sig_ss7_find_linkset(ss7);
	if (!winner) {
		return;
	}
	for (i = 0; i < winner->numchans; i++) {
		if (winner->pvts[i] && (winner->pvts[i]->ss7call == call)) {
			if (lock) {
				sig_ss7_lock_private(winner->pvts[i]);
			}
			winner->pvts[i]->ss7call = NULL;
			if (winner->pvts[i]->owner) {
				ast_channel_hangupcause_set(winner->pvts[i]->owner, AST_CAUSE_NORMAL_TEMPORARY_FAILURE);
				ast_channel_softhangup_internal_flag_add(winner->pvts[i]->owner, AST_SOFTHANGUP_DEV);
			}
			if (lock) {
				sig_ss7_unlock_private(winner->pvts[i]);
			}
			ast_log(LOG_WARNING, "libss7 asked set ss7 call to NULL on CIC %d DPC %d\n", winner->pvts[i]->cic, winner->pvts[i]->dpc);
		}
	}
}

void sig_ss7_cb_notinservice(struct ss7 *ss7, int cic, unsigned int dpc)
{
	struct sig_ss7_chan *p;

	if (!(p = ss7_find_pvt(ss7, cic, dpc))) {
		return;
	}

	sig_ss7_lock_private(p);
	sig_ss7_set_inservice(p, 0);
	sig_ss7_unlock_private(p);
}

/*!
 * \internal
 * \brief Check if CICs in a range belong to the linkset for a given DPC.
 * \since 11.0
 *
 * \param linkset SS7 linkset control structure.
 * \param startcic Circuit Identification Code to start from
 * \param endcic Circuit Identification Code to search up-to
 * \param dpc Destination Point Code
 * \param state Array containing the status of the search
 *
 * \retval Nothing.
 */
static void ss7_check_range(struct sig_ss7_linkset *linkset, int startcic, int endcic, unsigned int dpc, unsigned char *state)
{
	int cic;

	for (cic = startcic; cic <= endcic; cic++) {
		if (state[cic - startcic] && sig_ss7_find_cic(linkset, cic, dpc) == -1) {
			state[cic - startcic] = 0;
		}
	}
}

static int ss7_match_range(struct sig_ss7_chan *pvt, int startcic, int endcic, unsigned int dpc)
{
	if (pvt && pvt->dpc == dpc && pvt->cic >= startcic && pvt->cic <= endcic) {
		return 1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Check if a range is defined for the given DPC.
 * \since 11.0
 *
 * \param linkset SS7 linkset control structure.
 * \param startcic Start CIC of the range to clear.
 * \param endcic End CIC of the range to clear.
 * \param dpc Destination Point Code.
 *
 * \note Assumes the linkset->lock is already obtained.
 *
 * \return TRUE if all CICs in the range are present
 */
int sig_ss7_find_cic_range(struct sig_ss7_linkset *linkset, int startcic, int endcic, unsigned int dpc)
{
	int i, found = 0;

	for (i = 0; i < linkset->numchans; i++) {
		if (ss7_match_range(linkset->pvts[i], startcic, endcic, dpc)) {
			found++;
		}
	}

	if (found == endcic - startcic + 1) {
		return  1;
	}

	return 0;
}

static void ss7_handle_cqm(struct sig_ss7_linkset *linkset, ss7_event *e)
{
	unsigned char status[32];
	struct sig_ss7_chan *p = NULL;
	int i;
	int offset;
	int chanpos;

	memset(status, 0, sizeof(status));
	for (i = 0; i < linkset->numchans; i++) {
		if (ss7_match_range(linkset->pvts[i], e->cqm.startcic, e->cqm.endcic, e->cqm.opc)) {
			p = linkset->pvts[i];
			sig_ss7_lock_private(p);
			offset = p->cic - e->cqm.startcic;
			status[offset] = 0;
			if (p->locallyblocked) {
				status[offset] |= (1 << 0) | (1 << 4);
			}
			if (p->remotelyblocked) {
				status[offset] |= (1 << 1) | (1 << 5);
			}
			if (p->ss7call) {
				if (p->outgoing) {
					status[offset] |= (1 << 3);
				} else {
					status[offset] |= (1 << 2);
				}
			} else {
				status[offset] |= 0x3 << 2;
			}
			sig_ss7_unlock_private(p);
		}
	}

	if (p) {
		isup_cqr(linkset->ss7, e->cqm.startcic, e->cqm.endcic, e->cqm.opc, status);
	} else {
		ast_log(LOG_WARNING, "Could not find any equipped circuits within CQM CICs\n");
	}

	chanpos = sig_ss7_find_cic(linkset, e->cqm.startcic, e->cqm.opc);
	if (chanpos < 0) {
		isup_free_call(linkset->ss7, e->cqm.call);
		return;
	}
	p = linkset->pvts[chanpos];
	sig_ss7_lock_private(p);
	p->ss7call = e->cqm.call;
	if (!p->owner) {
		p->ss7call = isup_free_call_if_clear(linkset->ss7, e->cqm.call);
	}
	sig_ss7_unlock_private(p);
}

static inline void ss7_hangup_cics(struct sig_ss7_linkset *linkset, int startcic, int endcic, unsigned int dpc)
{
	int i;

	for (i = 0; i < linkset->numchans; i++) {
		if (ss7_match_range(linkset->pvts[i], startcic, endcic, dpc)) {
			sig_ss7_lock_private(linkset->pvts[i]);
			sig_ss7_lock_owner(linkset, i);
			if (linkset->pvts[i]->owner) {
				ast_softhangup_nolock(linkset->pvts[i]->owner, AST_SOFTHANGUP_DEV);
				ast_channel_unlock(linkset->pvts[i]->owner);
			}
			sig_ss7_unlock_private(linkset->pvts[i]);
		}
	}
}

/*!
 * \param linkset SS7 linkset control structure.
 * \param startcic Start CIC of the range to clear.
 * \param endcic End CIC of the range to clear.
 * \param dpc Destination Point Code.
 * \param state Affected CICs from the operation. NULL for all CICs in the range.
 * \param block Operation to perform. TRUE to block.
 * \param remotely Direction of the blocking. TRUE to block/unblock remotely.
 * \param type Blocking type - hardware or maintenance.
 *
 * \note Assumes the linkset->lock is already obtained.
 * \note Must be called without sig_ss7_lock_private() obtained.
 *
 * \return Nothing.
 */
static inline void ss7_block_cics(struct sig_ss7_linkset *linkset, int startcic, int endcic, unsigned int dpc, unsigned char state[], int block, int remotely, int type)
{
	int i;

	for (i = 0; i < linkset->numchans; i++) {
		if (ss7_match_range(linkset->pvts[i], startcic, endcic, dpc)) {
			sig_ss7_lock_private(linkset->pvts[i]);
			if (state) {
				if (state[linkset->pvts[i]->cic - startcic]) {

					if (remotely) {
						sig_ss7_set_remotelyblocked(linkset->pvts[i], block, type);
					} else {
						sig_ss7_set_locallyblocked(linkset->pvts[i], block, type);
					}

					sig_ss7_lock_owner(linkset, i);
					if (linkset->pvts[i]->owner) {
						if (ast_channel_state(linkset->pvts[i]->owner) == AST_STATE_DIALING
							&& linkset->pvts[i]->call_level < SIG_SS7_CALL_LEVEL_PROCEEDING) {
							ast_channel_hangupcause_set(linkset->pvts[i]->owner, SS7_CAUSE_TRY_AGAIN);
						}
						ast_channel_unlock(linkset->pvts[i]->owner);
					}
				}
			} else {
				if (remotely) {
					sig_ss7_set_remotelyblocked(linkset->pvts[i], block, type);
				} else {
					sig_ss7_set_locallyblocked(linkset->pvts[i], block, type);
				}
			}
			sig_ss7_unlock_private(linkset->pvts[i]);
		}
	}
}

/*!
 * \param linkset SS7 linkset control structure.
 * \param startcic Start CIC of the range to set in service.
 * \param endcic End CIC of the range to set in service.
 * \param dpc Destination Point Code.
 *
 * \note Must be called without sig_ss7_lock_private() obtained.
 *
 * \return Nothing.
 */
static void ss7_inservice(struct sig_ss7_linkset *linkset, int startcic, int endcic, unsigned int dpc)
{
	int i;

	for (i = 0; i < linkset->numchans; i++) {
		if (ss7_match_range(linkset->pvts[i], startcic, endcic, dpc)) {
			sig_ss7_lock_private(linkset->pvts[i]);
			sig_ss7_set_inservice(linkset->pvts[i], 1);
			sig_ss7_unlock_private(linkset->pvts[i]);
		}
	}
}

static int ss7_find_alloc_call(struct sig_ss7_chan *p)
{
	if (!p) {
		return 0;
	}

	if (!p->ss7call) {
		p->ss7call = isup_new_call(p->ss7->ss7, p->cic, p->dpc, 0);
		if (!p->ss7call) {
			return 0;
		}
	}
	return 1;
}

/*
 * XXX This routine is not tolerant of holes in the pvts[] array.
 * XXX This routine assumes the cic's in the pvts[] array are sorted.
 *
 * Probably the easiest way to deal with the invalid assumptions
 * is to have a local pvts[] array and sort it by dpc and cic.
 * Then the existing algorithm could work.
 */
static void ss7_reset_linkset(struct sig_ss7_linkset *linkset)
{
	int i, startcic, endcic, dpc;
	struct sig_ss7_chan *p;

	if (linkset->numchans <= 0) {
		return;
	}

	startcic = linkset->pvts[0]->cic;
	p = linkset->pvts[0];
	/* DB: CIC's DPC fix */
	dpc = linkset->pvts[0]->dpc;

	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i+1]
			&& linkset->pvts[i+1]->dpc == dpc
			&& linkset->pvts[i+1]->cic - linkset->pvts[i]->cic == 1
			&& linkset->pvts[i]->cic - startcic < (linkset->type == SS7_ANSI ? 24 : 31)) {
			continue;
		} else {
			endcic = linkset->pvts[i]->cic;
			ast_verb(1, "Resetting CICs %d to %d\n", startcic, endcic);

			sig_ss7_lock_private(p);
			if (!ss7_find_alloc_call(p)) {
				ast_log(LOG_ERROR, "Unable to allocate new ss7call\n");
			} else if (!(endcic - startcic)) {	/* GRS range can not be 0 - use RSC instead */
				isup_rsc(linkset->ss7, p->ss7call);
			} else {
				isup_grs(linkset->ss7, p->ss7call, endcic);
			}
			sig_ss7_unlock_private(p);

			/* DB: CIC's DPC fix */
			if (linkset->pvts[i+1]) {
				startcic = linkset->pvts[i+1]->cic;
				dpc = linkset->pvts[i+1]->dpc;
				p = linkset->pvts[i+1];
			}
		}
	}
}

/*!
 * \internal
 * \brief Complete the RSC procedure started earlier
 * \since 11.0
 *
 * \param p Signaling private structure pointer.
 *
 * \note Assumes the ss7->lock is already obtained.
 * \note Assumes sig_ss7_lock_private(p) is already obtained.
 *
 * \return Nothing.
 */
static void ss7_do_rsc(struct sig_ss7_chan *p)
{
	if (!p || !p->ss7call) {
		return;
	}

	isup_rsc(p->ss7->ss7, p->ss7call);

	if (p->locallyblocked & SS7_BLOCKED_MAINTENANCE) {
		isup_blo(p->ss7->ss7, p->ss7call);
	} else {
		sig_ss7_set_locallyblocked(p, 0, SS7_BLOCKED_MAINTENANCE | SS7_BLOCKED_HARDWARE);
	}
}

/*!
 * \internal
 * \brief Start RSC procedure on a specific link
 * \since 11.0
 *
 * \param ss7 SS7 linkset control structure.
 * \param which Channel position in the span.
 *
 * \note Assumes the ss7->lock is already obtained.
 * \note Assumes the sig_ss7_lock_private(ss7->pvts[chanpos]) is already obtained.
 *
 * \return TRUE on success
 */
static int ss7_start_rsc(struct sig_ss7_linkset *linkset, int which)
{
	if (!linkset->pvts[which]) {
		return 0;
	}

	if (!ss7_find_alloc_call(linkset->pvts[which])) {
		return 0;
	}

	sig_ss7_set_remotelyblocked(linkset->pvts[which], 0, SS7_BLOCKED_MAINTENANCE | SS7_BLOCKED_HARDWARE);
	sig_ss7_set_inservice(linkset->pvts[which], 0);
	sig_ss7_loopback(linkset->pvts[which], 0);

	sig_ss7_lock_owner(linkset, which);
	if (linkset->pvts[which]->owner) {
		ast_channel_hangupcause_set(linkset->pvts[which]->owner, AST_CAUSE_NORMAL_CLEARING);
		ast_softhangup_nolock(linkset->pvts[which]->owner, AST_SOFTHANGUP_DEV);
		ast_channel_unlock(linkset->pvts[which]->owner);
		linkset->pvts[which]->do_hangup = SS7_HANGUP_SEND_RSC;
	} else {
		ss7_do_rsc(linkset->pvts[which]);
	}

	return 1;
}

/*!
 * \internal
 * \brief Determine if a private channel structure is available.
 * \since 11.0
 *
 * \param linkset SS7 linkset control structure.
 * \param startcic Start CIC of the range to clear.
 * \param endcic End CIC of the range to clear.
 * \param dpc Destination Point Code.
 * \param do_hangup What we have to do to clear the call.
 *
 * \note Assumes the linkset->lock is already obtained.
 * \note Must be called without sig_ss7_lock_private() obtained.
 *
 * \return Nothing.
 */
static void ss7_clear_channels(struct sig_ss7_linkset *linkset, int startcic, int endcic, int dpc, int do_hangup)
{
	int i;

	for (i = 0; i < linkset->numchans; i++) {
		if (ss7_match_range(linkset->pvts[i], startcic, endcic, dpc)) {
			sig_ss7_lock_private(linkset->pvts[i]);
			sig_ss7_set_inservice(linkset->pvts[i], 0);
			sig_ss7_lock_owner(linkset, i);
			if (linkset->pvts[i]->owner) {
				ast_channel_hangupcause_set(linkset->pvts[i]->owner,
											AST_CAUSE_NORMAL_CLEARING);
				ast_softhangup_nolock(linkset->pvts[i]->owner, AST_SOFTHANGUP_DEV);
				ast_channel_unlock(linkset->pvts[i]->owner);
				linkset->pvts[i]->do_hangup = (linkset->pvts[i]->cic != startcic) ?
											do_hangup : SS7_HANGUP_DO_NOTHING;
			} else if (linkset->pvts[i] && linkset->pvts[i]->cic != startcic) {
				isup_free_call(linkset->pvts[i]->ss7->ss7, linkset->pvts[i]->ss7call);
				linkset->pvts[i]->ss7call = NULL;
			}
			sig_ss7_unlock_private(linkset->pvts[i]);
		}
	}
}

/*!
 * \internal
 *
 * \param p Signaling private structure pointer.
 * \param linkset SS7 linkset control structure.
 *
 * \note Assumes the linkset->lock is already obtained.
 * \note Assumes the sig_ss7_lock_private(ss7->pvts[chanpos]) is already obtained.
 *
 * \return Nothing.
 */
static void ss7_start_call(struct sig_ss7_chan *p, struct sig_ss7_linkset *linkset)
{
	struct ss7 *ss7 = linkset->ss7;
	int law;
	struct ast_channel *c;
	char tmp[256];
	char *strp;
	ast_callid callid = 0;
	int callid_created = ast_callid_threadstorage_auto(&callid);

	if (!(linkset->flags & LINKSET_FLAG_EXPLICITACM)) {
		p->call_level = SIG_SS7_CALL_LEVEL_PROCEEDING;
		isup_acm(ss7, p->ss7call);
	} else {
		p->call_level = SIG_SS7_CALL_LEVEL_SETUP;
	}

	/* Companding law is determined by SS7 signaling type. */
	if (linkset->type == SS7_ITU) {
		law = SIG_SS7_ALAW;
	} else {
		law = SIG_SS7_ULAW;
	}

	isup_set_echocontrol(p->ss7call, (linkset->flags & LINKSET_FLAG_DEFAULTECHOCONTROL) ? 1 : 0);

	/*
	 * Release the SS7 lock while we create the channel so other
	 * threads can send messages.  We must also release the private
	 * lock to prevent deadlock while creating the channel.
	 */
	ast_mutex_unlock(&linkset->lock);
	sig_ss7_unlock_private(p);
	c = sig_ss7_new_ast_channel(p, AST_STATE_RING, law, 0, p->exten, NULL, NULL);
	if (!c) {
		ast_log(LOG_WARNING, "Unable to start PBX on CIC %d\n", p->cic);
		ast_mutex_lock(&linkset->lock);
		sig_ss7_lock_private(p);
		isup_rel(linkset->ss7, p->ss7call, AST_CAUSE_SWITCH_CONGESTION);
		p->call_level = SIG_SS7_CALL_LEVEL_IDLE;
		ast_callid_threadstorage_auto_clean(callid, callid_created);
		return;
	}

	/* Hold the channel and private lock while we setup the channel. */
	ast_channel_lock(c);
	sig_ss7_lock_private(p);

	ast_channel_stage_snapshot(c);

	/*
	 * It is reasonably safe to set the following
	 * channel variables while the channel private
	 * structure is locked.  The PBX has not been
	 * started yet and it is unlikely that any other task
	 * will do anything with the channel we have just
	 * created.
	 *
	 * We only reference these variables in the context of the ss7_linkset function
	 * when receiving either and IAM or a COT message.
	 */
	if (!ast_strlen_zero(p->charge_number)) {
		pbx_builtin_setvar_helper(c, "SS7_CHARGE_NUMBER", p->charge_number);
		/* Clear this after we set it */
		p->charge_number[0] = 0;
	}
	if (!ast_strlen_zero(p->gen_add_number)) {
		pbx_builtin_setvar_helper(c, "SS7_GENERIC_ADDRESS", p->gen_add_number);
		/* Clear this after we set it */
		p->gen_add_number[0] = 0;
	}
	if (!ast_strlen_zero(p->jip_number)) {
		pbx_builtin_setvar_helper(c, "SS7_JIP", p->jip_number);
		/* Clear this after we set it */
		p->jip_number[0] = 0;
	}
	if (!ast_strlen_zero(p->gen_dig_number)) {
		pbx_builtin_setvar_helper(c, "SS7_GENERIC_DIGITS", p->gen_dig_number);
		/* Clear this after we set it */
		p->gen_dig_number[0] = 0;
	}

	snprintf(tmp, sizeof(tmp), "%d", p->gen_dig_type);
	pbx_builtin_setvar_helper(c, "SS7_GENERIC_DIGTYPE", tmp);
	/* Clear this after we set it */
	p->gen_dig_type = 0;

	snprintf(tmp, sizeof(tmp), "%d", p->gen_dig_scheme);
	pbx_builtin_setvar_helper(c, "SS7_GENERIC_DIGSCHEME", tmp);
	/* Clear this after we set it */
	p->gen_dig_scheme = 0;

	if (!ast_strlen_zero(p->lspi_ident)) {
		pbx_builtin_setvar_helper(c, "SS7_LSPI_IDENT", p->lspi_ident);
		/* Clear this after we set it */
		p->lspi_ident[0] = 0;
	}

	snprintf(tmp, sizeof(tmp), "%d", p->call_ref_ident);
	pbx_builtin_setvar_helper(c, "SS7_CALLREF_IDENT", tmp);
	/* Clear this after we set it */
	p->call_ref_ident = 0;

	snprintf(tmp, sizeof(tmp), "%d", p->call_ref_pc);
	pbx_builtin_setvar_helper(c, "SS7_CALLREF_PC", tmp);
	/* Clear this after we set it */
	p->call_ref_pc = 0;

	snprintf(tmp, sizeof(tmp), "%d", p->calling_party_cat);
	pbx_builtin_setvar_helper(c, "SS7_CALLING_PARTY_CATEGORY", tmp);
	/* Clear this after we set it */
	p->calling_party_cat = 0;

	if (p->redirect_counter) {
		struct ast_party_redirecting redirecting;

		switch (p->redirect_info_ind) {
		case 0:
			strp = "NO_REDIRECTION";
			break;
		case 1:
			strp = "CALL_REROUTED_PRES_ALLOWED";
			break;
		case 2:
			strp = "CALL_REROUTED_INFO_RESTRICTED";
			break;
		case 3:
			strp = "CALL_DIVERTED_PRES_ALLOWED";
			break;
		case 4:
			strp = "CALL_DIVERTED_INFO_RESTRICTED";
			break;
		case 5:
			strp = "CALL_REROUTED_PRES_RESTRICTED";
			break;
		case 6:
			strp = "CALL_DIVERTED_PRES_RESTRICTED";
			break;
		case 7:
			strp = "SPARE";
			break;
		default:
			strp = "NO_REDIRECTION";
			break;
		}
		pbx_builtin_setvar_helper(c, "SS7_REDIRECT_INFO_IND", strp);
		/* Clear this after we set it */
		p->redirect_info_ind = 0;

		ast_party_redirecting_init(&redirecting);

		if (p->redirect_info_counter) {
			redirecting.count = p->redirect_info_counter;
			if (p->redirect_info_counter != p->redirect_counter) {
				if (p->redirect_info_counter < p->redirect_counter) {
					redirecting.count = p->redirect_counter;
				}
				ast_log(LOG_WARNING, "Redirect counters differ: %u while info says %u - using %u\n",
					p->redirect_counter, p->redirect_info_counter, redirecting.count);
			}
			/* Clear this after we set it */
			p->redirect_info_counter = 0;
			p->redirect_counter = 0;
		}

		if (p->redirect_counter) {
			redirecting.count = p->redirect_counter;
			/* Clear this after we set it */
			p->redirect_counter = 0;
		}

		switch (p->redirect_info_orig_reas) {
		case SS7_REDIRECTING_REASON_UNKNOWN:
			redirecting.orig_reason.code = AST_REDIRECTING_REASON_UNKNOWN;
			break;
		case SS7_REDIRECTING_REASON_USER_BUSY:
			redirecting.orig_reason.code = AST_REDIRECTING_REASON_USER_BUSY;
			break;
		case SS7_REDIRECTING_REASON_NO_ANSWER:
			redirecting.orig_reason.code = AST_REDIRECTING_REASON_NO_ANSWER;
			break;
		case SS7_REDIRECTING_REASON_UNCONDITIONAL:
			redirecting.orig_reason.code = AST_REDIRECTING_REASON_UNCONDITIONAL;
			break;
		default:
			redirecting.orig_reason.code = AST_REDIRECTING_REASON_UNKNOWN;
			break;
		}

		switch (p->redirect_info_reas) {
		case SS7_REDIRECTING_REASON_UNKNOWN:
			redirecting.reason.code = AST_REDIRECTING_REASON_UNKNOWN;
			break;
		case SS7_REDIRECTING_REASON_USER_BUSY:
			redirecting.reason.code = AST_REDIRECTING_REASON_USER_BUSY;
			if (!p->redirect_info_orig_reas && redirecting.count == 1) {
				redirecting.orig_reason.code = AST_REDIRECTING_REASON_USER_BUSY;
			}
			break;
		case SS7_REDIRECTING_REASON_NO_ANSWER:
			redirecting.reason.code = AST_REDIRECTING_REASON_NO_ANSWER;
			if (!p->redirect_info_orig_reas && redirecting.count == 1) {
				redirecting.orig_reason.code = AST_REDIRECTING_REASON_NO_ANSWER;
			}
			break;
		case SS7_REDIRECTING_REASON_UNCONDITIONAL:
			redirecting.reason.code = AST_REDIRECTING_REASON_UNCONDITIONAL;
			if (!p->redirect_info_orig_reas && redirecting.count == 1) {
				redirecting.orig_reason.code = AST_REDIRECTING_REASON_UNCONDITIONAL;
			}
			break;
		case SS7_REDIRECTING_REASON_DEFLECTION_DURING_ALERTING:
		case SS7_REDIRECTING_REASON_DEFLECTION_IMMEDIATE_RESPONSE:
			redirecting.reason.code = AST_REDIRECTING_REASON_DEFLECTION;
			break;
		case SS7_REDIRECTING_REASON_UNAVAILABLE:
			redirecting.reason.code = AST_REDIRECTING_REASON_UNAVAILABLE;
			break;
		default:
			redirecting.reason.code = AST_REDIRECTING_REASON_UNKNOWN;
			break;
		}
		/* Clear this after we set it */
		p->redirect_info_orig_reas = 0;
		p->redirect_info_reas = 0;

		if (!ast_strlen_zero(p->redirecting_num)) {
			redirecting.from.number.str = ast_strdup(p->redirecting_num);
			redirecting.from.number.presentation = p->redirecting_presentation;
			redirecting.from.number.valid = 1;
			/* Clear this after we set it */
			p->redirecting_num[0] = 0;
		}

		if (!ast_strlen_zero(p->generic_name)) {
			redirecting.from.name.str = ast_strdup(p->generic_name);
			redirecting.from.name.presentation = p->redirecting_presentation;
			redirecting.from.name.valid = 1;
			/* Clear this after we set it */
			p->generic_name[0] = 0;
		}

		if (!ast_strlen_zero(p->orig_called_num)) {
			redirecting.orig.number.str = ast_strdup(p->orig_called_num);
			redirecting.orig.number.presentation = p->orig_called_presentation;
			redirecting.orig.number.valid = 1;
			/* Clear this after we set it */
			p->orig_called_num[0] = 0;
		} else if (redirecting.count == 1) {
			ast_party_id_copy(&redirecting.orig, &redirecting.from);
		}

		ast_channel_update_redirecting(c, &redirecting, NULL);
		ast_party_redirecting_free(&redirecting);
	}

	if (p->cug_indicator != ISUP_CUG_NON) {
		sprintf(tmp, "%d", p->cug_interlock_code);
		pbx_builtin_setvar_helper(c, "SS7_CUG_INTERLOCK_CODE", tmp);

		switch (p->cug_indicator) {
		case ISUP_CUG_NON:
			strp = "NON_CUG";
			break;
		case ISUP_CUG_OUTGOING_ALLOWED:
			strp = "OUTGOING_ALLOWED";
			break;
		case ISUP_CUG_OUTGOING_NOT_ALLOWED:
			strp = "OUTGOING_NOT_ALLOWED";
			break;
		default:
			strp = "SPARE";
			break;
		}
		pbx_builtin_setvar_helper(c, "SS7_CUG_INDICATOR", strp);

		if (!ast_strlen_zero(p->cug_interlock_ni)) {
			pbx_builtin_setvar_helper(c, "SS7_CUG_INTERLOCK_NI", p->cug_interlock_ni);
		}

		p->cug_indicator = ISUP_CUG_NON;
	}

	ast_channel_stage_snapshot_done(c);

	sig_ss7_unlock_private(p);
	ast_channel_unlock(c);

	if (ast_pbx_start(c)) {
		ast_log(LOG_WARNING, "Unable to start PBX on %s (CIC %d)\n", ast_channel_name(c), p->cic);
		ast_hangup(c);
	} else {
		ast_verb(3, "Accepting call to '%s' on CIC %d\n", p->exten, p->cic);
	}

	/* Must return with linkset and private lock. */
	ast_mutex_lock(&linkset->lock);
	sig_ss7_lock_private(p);
	ast_callid_threadstorage_auto_clean(callid, callid_created);
}

static void ss7_apply_plan_to_number(char *buf, size_t size, const struct sig_ss7_linkset *ss7, const char *number, const unsigned nai)
{
	if (ast_strlen_zero(number)) { /* make sure a number exists so prefix isn't placed on an empty string */
		if (size) {
			*buf = '\0';
		}
		return;
	}
	switch (nai) {
	case SS7_NAI_INTERNATIONAL:
		snprintf(buf, size, "%s%s", ss7->internationalprefix, number);
		break;
	case SS7_NAI_NATIONAL:
		snprintf(buf, size, "%s%s", ss7->nationalprefix, number);
		break;
	case SS7_NAI_SUBSCRIBER:
		snprintf(buf, size, "%s%s", ss7->subscriberprefix, number);
		break;
	case SS7_NAI_UNKNOWN:
		snprintf(buf, size, "%s%s", ss7->unknownprefix, number);
		break;
	case SS7_NAI_NETWORKROUTED:
		snprintf(buf, size, "%s%s", ss7->networkroutedprefix, number);
		break;
	default:
		snprintf(buf, size, "%s", number);
		break;
	}
}

static int ss7_pres_scr2cid_pres(char presentation_ind, char screening_ind)
{
	return ((presentation_ind & 0x3) << 5) | (screening_ind & 0x3);
}

/*!
 * \internal
 * \brief Set callid threadstorage for the ss7_linkset thread to that of an existing channel
 *
 * \param linkset ss7 span control structure.
 * \param chanpos channel position in the span
 *
 * \note Assumes the ss7->lock is already obtained.
 * \note Assumes the sig_ss7_lock_private(ss7->pvts[chanpos]) is already obtained.
 *
 * \return The callid bound to the channel which has also been bound to threadstorage
 *         if it exists. If this returns non-zero, the threadstorage should be unbound
 *         before the while loop wraps in ss7_linkset.
 */
static ast_callid func_ss7_linkset_callid(struct sig_ss7_linkset *linkset, int chanpos)
{
	ast_callid callid = 0;
	sig_ss7_lock_owner(linkset, chanpos);
	if (linkset->pvts[chanpos]->owner) {
		callid = ast_channel_callid(linkset->pvts[chanpos]->owner);
		ast_channel_unlock(linkset->pvts[chanpos]->owner);
		if (callid) {
			ast_callid_threadassoc_add(callid);
		}
	}

	return callid;
}

/*!
 * \internal
 * \brief Proceed with the call based on the extension matching status
 * is matching in the dialplan.
 * \since 11.0
 *
 * \param linkset ss7 span control structure.
 * \param p Signaling private structure pointer.
 * \param e Event causing the match.
 *
 * \note Assumes the linkset->lock is already obtained.
 * \note Assumes the sig_ss7_lock_private(ss7->pvts[chanpos]) is already obtained.
 *
 * \return Nothing.
 */
static void ss7_match_extension(struct sig_ss7_linkset *linkset, struct sig_ss7_chan *p, ss7_event *e)
{
	ast_verb(3, "SS7 exten: %s complete: %d\n", p->exten, p->called_complete);

	if (!p->called_complete
		&& linkset->type == SS7_ITU /* ANSI does not support overlap dialing. */
		&& ast_matchmore_extension(NULL, p->context, p->exten, 1, p->cid_num)
		&& !isup_start_digittimeout(linkset->ss7, p->ss7call)) {
		/* Wait for more digits. */
		return;
	}
	if (ast_exists_extension(NULL, p->context, p->exten, 1, p->cid_num)) {
		/* DNID is complete */
		p->called_complete = 1;
		sig_ss7_set_dnid(p, p->exten);

		/* If COT successful start call! */
		if ((e->e == ISUP_EVENT_IAM)
			? !(e->iam.cot_check_required || e->iam.cot_performed_on_previous_cic)
			: (!(e->sam.cot_check_required || e->sam.cot_performed_on_previous_cic) || e->sam.cot_check_passed)) {
			ss7_start_call(p, linkset);
		}
		return;
	}

	ast_debug(1, "Call on CIC for unconfigured extension %s\n", p->exten);
	isup_rel(linkset->ss7, (e->e == ISUP_EVENT_IAM) ? e->iam.call : e->sam.call, AST_CAUSE_UNALLOCATED);
}

/* This is a thread per linkset that handles all received events from libss7. */
void *ss7_linkset(void *data)
{
	int res, i;
	struct timeval *next = NULL, tv;
	struct sig_ss7_linkset *linkset = (struct sig_ss7_linkset *) data;
	struct ss7 *ss7 = linkset->ss7;
	ss7_event *e = NULL;
	struct sig_ss7_chan *p;
	struct pollfd pollers[SIG_SS7_NUM_DCHANS];
	unsigned char mb_state[255];
	int nextms;

#define SS7_MAX_POLL	60000	/* Maximum poll time in ms. */

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	ss7_set_debug(ss7, SIG_SS7_DEBUG_DEFAULT);
	ast_mutex_lock(&linkset->lock);
	ss7_start(ss7);
	ast_mutex_unlock(&linkset->lock);

	for (;;) {
		ast_mutex_lock(&linkset->lock);
		if ((next = ss7_schedule_next(ss7))) {
			tv = ast_tvnow();
			tv.tv_sec = next->tv_sec - tv.tv_sec;
			tv.tv_usec = next->tv_usec - tv.tv_usec;
			if (tv.tv_usec < 0) {
				tv.tv_usec += 1000000;
				tv.tv_sec -= 1;
			}
			if (tv.tv_sec < 0) {
				tv.tv_sec = 0;
				tv.tv_usec = 0;
			}
			nextms = tv.tv_sec * 1000;
			nextms += tv.tv_usec / 1000;
			if (SS7_MAX_POLL < nextms) {
				nextms = SS7_MAX_POLL;
			}
		} else {
			nextms = SS7_MAX_POLL;
		}

		for (i = 0; i < linkset->numsigchans; i++) {
			pollers[i].fd = linkset->fds[i];
			pollers[i].events = ss7_pollflags(ss7, linkset->fds[i]);
			pollers[i].revents = 0;
		}
		ast_mutex_unlock(&linkset->lock);

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		pthread_testcancel();
		res = poll(pollers, linkset->numsigchans, nextms);
		pthread_testcancel();
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		ast_mutex_lock(&linkset->lock);
		if ((res < 0) && (errno != EINTR)) {
			ast_log(LOG_ERROR, "poll(%s)\n", strerror(errno));
		} else if (!res) {
			ss7_schedule_run(ss7);
		}

		for (i = 0; i < linkset->numsigchans; i++) {
			if (pollers[i].revents & POLLPRI) {
				sig_ss7_handle_link_exception(linkset, i);
			}
			if (pollers[i].revents & POLLIN) {
				res = ss7_read(ss7, pollers[i].fd);
			}
			if (pollers[i].revents & POLLOUT) {
				res = ss7_write(ss7, pollers[i].fd);
				if (res < 0) {
					ast_debug(1, "Error in write %s\n", strerror(errno));
				}
			}
		}

		while ((e = ss7_check_event(ss7))) {
			ast_callid callid = 0;
			int chanpos = -1;
			char cause_str[30];

			if (linkset->debug) {
				ast_verbose("Linkset %d: Processing event: %s\n",
					linkset->span, ss7_event2str(e->e));
			}

			switch (e->e) {
			case SS7_EVENT_UP:
				if (linkset->state != LINKSET_STATE_UP) {
					ast_verb(1, "--- SS7 Up ---\n");
					ss7_reset_linkset(linkset);
				}
				linkset->state = LINKSET_STATE_UP;
				break;
			case SS7_EVENT_DOWN:
				ast_verb(1, "--- SS7 Down ---\n");
				linkset->state = LINKSET_STATE_DOWN;
				for (i = 0; i < linkset->numchans; i++) {
					p = linkset->pvts[i];
					if (p) {
						sig_ss7_set_inservice(p, 0);
						if (linkset->flags & LINKSET_FLAG_INITIALHWBLO) {
							sig_ss7_set_remotelyblocked(p, 1, SS7_BLOCKED_HARDWARE);
						}
					}
				}
				break;
			case MTP2_LINK_UP:
				ast_verb(1, "MTP2 link up (SLC %d)\n", e->gen.data);
				break;
			case MTP2_LINK_DOWN:
				ast_log(LOG_WARNING, "MTP2 link down (SLC %d)\n", e->gen.data);
				break;
			case ISUP_EVENT_CPG:
				chanpos = ss7_find_cic_gripe(linkset, e->cpg.cic, e->cpg.opc, "CPG");
				if (chanpos < 0) {
					isup_free_call(ss7, e->cpg.call);
					break;
				}
				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				callid = func_ss7_linkset_callid(linkset, chanpos);

				switch (e->cpg.event) {
				case CPG_EVENT_ALERTING:
					if (p->call_level < SIG_SS7_CALL_LEVEL_ALERTING) {
						p->call_level = SIG_SS7_CALL_LEVEL_ALERTING;
					}
					sig_ss7_lock_owner(linkset, chanpos);
					if (p->owner) {
						ast_setstate(p->owner, AST_STATE_RINGING);
						if (!ast_strlen_zero(e->cpg.connected_num)) {
							struct ast_party_connected_line ast_connected;
							char connected_num[AST_MAX_EXTENSION];

							ast_party_connected_line_init(&ast_connected);
							ast_connected.id.number.presentation =
								ss7_pres_scr2cid_pres(e->cpg.connected_presentation_ind,
								e->cpg.connected_screening_ind);
							ss7_apply_plan_to_number(connected_num, sizeof(connected_num),
								linkset, e->cpg.connected_num, e->cpg.connected_nai);
							ast_connected.id.number.str = ast_strdup(connected_num);
							ast_connected.id.number.valid = 1;
							ast_channel_queue_connected_line_update(p->owner, &ast_connected, NULL);
							ast_party_connected_line_free(&ast_connected);
						}
						ast_channel_unlock(p->owner);
					}
					sig_ss7_queue_control(linkset, chanpos, AST_CONTROL_RINGING);
					break;
				case CPG_EVENT_PROGRESS:
				case CPG_EVENT_INBANDINFO:
					{
						ast_debug(1, "Queuing frame PROGRESS on CIC %d\n", p->cic);
						sig_ss7_queue_control(linkset, chanpos, AST_CONTROL_PROGRESS);
						p->progress = 1;
						sig_ss7_set_dialing(p, 0);
						sig_ss7_open_media(p);
					}
					break;
				default:
					ast_debug(1, "Do not handle CPG with event type 0x%x\n", e->cpg.event);
					break;
				}

				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_RSC:
				ast_verb(1, "Resetting CIC %d\n", e->rsc.cic);
				chanpos = ss7_find_cic_gripe(linkset, e->rsc.cic, e->rsc.opc, "RSC");
				if (chanpos < 0) {
					isup_free_call(ss7, e->rsc.call);
					break;
				}
				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				p->ss7call = e->rsc.call;
				callid = func_ss7_linkset_callid(linkset, chanpos);
				sig_ss7_set_inservice(p, 1);
				sig_ss7_set_remotelyblocked(p, 0, SS7_BLOCKED_MAINTENANCE | SS7_BLOCKED_HARDWARE);

				if (p->locallyblocked & SS7_BLOCKED_MAINTENANCE) {
					isup_blo(ss7, e->rsc.call);
				} else if (p->locallyblocked & SS7_BLOCKED_HARDWARE) {
					sig_ss7_set_locallyblocked(p, 0, SS7_BLOCKED_HARDWARE);
				}

				isup_set_call_dpc(e->rsc.call, p->dpc);
				sig_ss7_lock_owner(linkset, chanpos);
				if (p->owner) {
					p->do_hangup = SS7_HANGUP_SEND_RLC;
					if (!(e->rsc.got_sent_msg & ISUP_SENT_IAM)) {
						/* Q.784 6.2.3 */
						ast_channel_hangupcause_set(p->owner, AST_CAUSE_NORMAL_CLEARING);
					} else {
						ast_channel_hangupcause_set(p->owner, SS7_CAUSE_TRY_AGAIN);
					}

					ss7_queue_pvt_cause_data(p->owner, "SS7 ISUP_EVENT_RSC", AST_CAUSE_INTERWORKING);

					ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_DEV);
					ast_channel_unlock(p->owner);
				} else {
					isup_rlc(ss7, e->rsc.call);
					p->ss7call = isup_free_call_if_clear(ss7, e->rsc.call);
				}
				/* End the loopback if we have one */
				sig_ss7_loopback(p, 0);

				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_GRS:
				if (!sig_ss7_find_cic_range(linkset, e->grs.startcic, e->grs.endcic,
					e->grs.opc)) {
					ast_log(LOG_WARNING, "GRS on unconfigured range CIC %d - %d PC %d\n",
						e->grs.startcic, e->grs.endcic, e->grs.opc);
					chanpos = sig_ss7_find_cic(linkset, e->grs.startcic, e->grs.opc);
					if (chanpos < 0) {
						isup_free_call(ss7, e->grs.call);
						break;
					}
					p = linkset->pvts[chanpos];
					sig_ss7_lock_private(p);
					p->ss7call = isup_free_call_if_clear(ss7, e->grs.call);
					sig_ss7_unlock_private(p);
					break;
				}

				/* Leave startcic last to collect all cics mb_state */
				for (i = e->grs.endcic - e->grs.startcic; 0 <= i; --i) {
					/*
					 * We are guaranteed to find chanpos because
					 * sig_ss7_find_cic_range() includes it.
					 */
					chanpos = sig_ss7_find_cic(linkset, e->grs.startcic + i, e->grs.opc);
					p = linkset->pvts[chanpos];
					sig_ss7_lock_private(p);

					if (p->locallyblocked & SS7_BLOCKED_MAINTENANCE) {
						mb_state[i] = 1;
					} else {
						mb_state[i] = 0;
						sig_ss7_set_locallyblocked(p, 0, SS7_BLOCKED_HARDWARE);
					}

					sig_ss7_set_remotelyblocked(p, 0, SS7_BLOCKED_MAINTENANCE | SS7_BLOCKED_HARDWARE);

					if (!i) {
						p->ss7call = e->grs.call;
						isup_gra(ss7, p->ss7call, e->grs.endcic, mb_state);
					}

					sig_ss7_lock_owner(linkset, chanpos);
					if (p->owner) {
						ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
						if (ast_channel_state(p->owner) == AST_STATE_DIALING
							&& linkset->pvts[i]->call_level < SIG_SS7_CALL_LEVEL_PROCEEDING) {
							ast_channel_hangupcause_set(p->owner, SS7_CAUSE_TRY_AGAIN);
						} else {
							ast_channel_hangupcause_set(p->owner, AST_CAUSE_NORMAL_CLEARING);
						}
						p->do_hangup = SS7_HANGUP_FREE_CALL;
						ast_channel_unlock(p->owner);
					} else if (!i) {
						p->ss7call = isup_free_call_if_clear(ss7, p->ss7call);
					} else if (p->ss7call) {
						/* clear any other session */
						isup_free_call(ss7, p->ss7call);
						p->ss7call = NULL;
					}
					sig_ss7_set_inservice(p, 1);
					sig_ss7_unlock_private(p);
				}
				break;
			case ISUP_EVENT_CQM:
				ast_debug(1, "Got Circuit group query message from CICs %d to %d\n",
							e->cqm.startcic, e->cqm.endcic);
				ss7_handle_cqm(linkset, e);
				break;
			case ISUP_EVENT_GRA:
				if (!sig_ss7_find_cic_range(linkset, e->gra.startcic,
							e->gra.endcic, e->gra.opc)) {	/* Never will be true */
					ast_log(LOG_WARNING, "GRA on unconfigured range CIC %d - %d PC %d\n",
							e->gra.startcic, e->gra.endcic, e->gra.opc);
					isup_free_call(ss7, e->gra.call);
					break;
				}
				ast_verb(1, "Got reset acknowledgement from CIC %d to %d DPC: %d\n",
					e->gra.startcic, e->gra.endcic, e->gra.opc);
				ss7_block_cics(linkset, e->gra.startcic, e->gra.endcic, e->gra.opc,
					e->gra.status, 1, 1, SS7_BLOCKED_MAINTENANCE);
				ss7_inservice(linkset, e->gra.startcic, e->gra.endcic, e->gra.opc);

				chanpos = sig_ss7_find_cic(linkset, e->gra.startcic, e->gra.opc);
				if (chanpos < 0) {
					isup_free_call(ss7, e->gra.call);
					break;
				}

				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);

				/* we may send a CBD with GRS! */
				p->ss7call = isup_free_call_if_clear(ss7, e->gra.call);
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_SAM:
				chanpos = ss7_find_cic_gripe(linkset, e->sam.cic, e->sam.opc, "SAM");
				if (chanpos < 0) {
					isup_free_call(ss7, e->sam.call);
					break;
				}
				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				sig_ss7_lock_owner(linkset, chanpos);
				if (p->owner) {
					ast_log(LOG_WARNING, "SAM on CIC %d PC %d already have call\n", e->sam.cic, e->sam.opc);
					ast_channel_unlock(p->owner);
					sig_ss7_unlock_private(p);
					break;
				}
				p->called_complete = 0;
				if (!ast_strlen_zero(e->sam.called_party_num)) {
					char *st;
					strncat(p->exten, e->sam.called_party_num, sizeof(p->exten) - strlen(p->exten) - 1);
					st = strchr(p->exten, '#');
					if (st) {
						*st = '\0';
						p->called_complete = 1;
					}
					ss7_match_extension(linkset, p, e);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_IAM:
				ast_debug(1, "Got IAM for CIC %d and called number %s, calling number %s\n", e->iam.cic, e->iam.called_party_num, e->iam.calling_party_num);
				chanpos = ss7_find_cic_gripe(linkset, e->iam.cic, e->iam.opc, "IAM");
				if (chanpos < 0) {
					isup_free_call(ss7, e->iam.call);
					break;
				}
				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				/*
				 * The channel should be idle and not have an owner at this point since we
				 * are in the process of creating an owner for it.
				 */
				ast_assert(!p->owner && p->call_level == SIG_SS7_CALL_LEVEL_IDLE);

				if (p->remotelyblocked) {
					ast_log(LOG_NOTICE, "Got IAM on remotely blocked CIC %d DPC %d remove blocking\n", e->iam.cic, e->iam.opc);
					sig_ss7_set_remotelyblocked(p, 0, SS7_BLOCKED_MAINTENANCE | SS7_BLOCKED_HARDWARE);
					sig_ss7_set_inservice(p, 1);
				}

				if (!sig_ss7_is_chan_available(p)) {
					/* Circuit is likely blocked or in alarm. */
					isup_rel(ss7, e->iam.call, AST_CAUSE_NORMAL_CIRCUIT_CONGESTION);
					if (p->locallyblocked) {
						isup_clear_callflags(ss7, e->iam.call, ISUP_GOT_IAM);
						p->ss7call = isup_free_call_if_clear(ss7, e->iam.call);
						ast_log(LOG_WARNING, "Got IAM on locally blocked CIC %d DPC %d, ignore\n", e->iam.cic, e->iam.opc);
					}
					sig_ss7_unlock_private(p);
					break;
				}

				/* Mark channel as in use so no outgoing call will steal it. */
				p->call_level = SIG_SS7_CALL_LEVEL_ALLOCATED;
				p->ss7call = e->iam.call;

				isup_set_call_dpc(p->ss7call, p->dpc);

				if ((p->use_callerid) && (!ast_strlen_zero(e->iam.calling_party_num))) {
					ss7_apply_plan_to_number(p->cid_num, sizeof(p->cid_num), linkset, e->iam.calling_party_num, e->iam.calling_nai);
					p->callingpres = ss7_pres_scr2cid_pres(e->iam.presentation_ind, e->iam.screening_ind);
				} else {
					p->cid_num[0] = 0;
					if (e->iam.presentation_ind) {
						p->callingpres = ss7_pres_scr2cid_pres(e->iam.presentation_ind, e->iam.screening_ind);
					}
				}

				p->called_complete = 0;
				if (p->immediate) {
					p->exten[0] = 's';
					p->exten[1] = '\0';
				} else if (!ast_strlen_zero(e->iam.called_party_num)) {
					char *st;
					ss7_apply_plan_to_number(p->exten, sizeof(p->exten), linkset, e->iam.called_party_num, e->iam.called_nai);
					st = strchr(p->exten, '#');
					if (st) {
						*st = '\0';
						p->called_complete = 1;
					}
				} else {
					p->exten[0] = '\0';
				}

				p->cid_ani[0] = '\0';
				if ((p->use_callerid) && (!ast_strlen_zero(e->iam.generic_name))) {
					ast_copy_string(p->cid_name, e->iam.generic_name, sizeof(p->cid_name));
				} else {
					p->cid_name[0] = '\0';
				}

				p->cid_ani2 = e->iam.oli_ani2;
				p->cid_ton = 0;
				ast_copy_string(p->charge_number, e->iam.charge_number, sizeof(p->charge_number));
				ast_copy_string(p->gen_add_number, e->iam.gen_add_number, sizeof(p->gen_add_number));
				p->gen_add_type = e->iam.gen_add_type;
				p->gen_add_nai = e->iam.gen_add_nai;
				p->gen_add_pres_ind = e->iam.gen_add_pres_ind;
				p->gen_add_num_plan = e->iam.gen_add_num_plan;
				ast_copy_string(p->gen_dig_number, e->iam.gen_dig_number, sizeof(p->gen_dig_number));
				p->gen_dig_type = e->iam.gen_dig_type;
				p->gen_dig_scheme = e->iam.gen_dig_scheme;
				ast_copy_string(p->jip_number, e->iam.jip_number, sizeof(p->jip_number));
				if (!ast_strlen_zero(e->iam.orig_called_num)) {
					ss7_apply_plan_to_number(p->orig_called_num, sizeof(p->orig_called_num), linkset, e->iam.orig_called_num, e->iam.orig_called_nai);
					p->orig_called_presentation = ss7_pres_scr2cid_pres(e->iam.orig_called_pres_ind, e->iam.orig_called_screening_ind);
				}
				if (!ast_strlen_zero(e->iam.redirecting_num)) {
					ss7_apply_plan_to_number(p->redirecting_num, sizeof(p->redirecting_num), linkset, e->iam.redirecting_num, e->iam.redirecting_num_nai);
					p->redirecting_presentation = ss7_pres_scr2cid_pres(e->iam.redirecting_num_presentation_ind, e->iam.redirecting_num_screening_ind);
				}
				ast_copy_string(p->generic_name, e->iam.generic_name, sizeof(p->generic_name));
				p->calling_party_cat = e->iam.calling_party_cat;
				p->redirect_counter = e->iam.redirect_counter;
				p->redirect_info = e->iam.redirect_info;
				p->redirect_info_ind = e->iam.redirect_info_ind;
				p->redirect_info_orig_reas = e->iam.redirect_info_orig_reas;
				p->redirect_info_counter = e->iam.redirect_info_counter;
				if (p->redirect_info_counter && !p->redirect_counter) {
					p->redirect_counter = p->redirect_info_counter;
				}
				p->redirect_info_reas = e->iam.redirect_info_reas;
				p->cug_indicator = e->iam.cug_indicator;
				p->cug_interlock_code = e->iam.cug_interlock_code;
				ast_copy_string(p->cug_interlock_ni, e->iam.cug_interlock_ni, sizeof(p->cug_interlock_ni));

				if (e->iam.cot_check_required) {
					sig_ss7_loopback(p, 1);
				}

				p->echocontrol_ind = e->iam.echocontrol_ind;
				sig_ss7_set_caller_id(p);
				ss7_match_extension(linkset, p, e);
				sig_ss7_unlock_private(p);

				if (e->iam.cot_performed_on_previous_cic) {
					chanpos = sig_ss7_find_cic(linkset, (e->iam.cic - 1), e->iam.opc);
					if (chanpos < 0) {
						/* some stupid switch do this */
						ast_verb(1, "COT request on previous nonexistent CIC %d in IAM PC %d\n", (e->iam.cic - 1), e->iam.opc);
						break;
					}
					ast_verb(1, "COT request on previous CIC %d in IAM PC %d\n", (e->iam.cic - 1), e->iam.opc);
					p = linkset->pvts[chanpos];
					sig_ss7_lock_private(p);
					if (sig_ss7_is_chan_available(p)) {
						sig_ss7_set_inservice(p, 0);	/* to prevent to use this circuit */
						sig_ss7_loopback(p, 1);
					} /* If already have a call don't loop */
					sig_ss7_unlock_private(p);
				}
				break;
			case ISUP_EVENT_DIGITTIMEOUT:
				chanpos = ss7_find_cic_gripe(linkset, e->digittimeout.cic, e->digittimeout.opc, "DIGITTIMEOUT");
				if (chanpos < 0) {
					isup_free_call(ss7, e->digittimeout.call);
					break;
				}
				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				ast_debug(1, "Digittimeout on CIC: %d PC: %d\n", e->digittimeout.cic, e->digittimeout.opc);
				if (ast_exists_extension(NULL, p->context, p->exten, 1, p->cid_num)) {
					/* DNID is complete */
					p->called_complete = 1;
					sig_ss7_set_dnid(p, p->exten);

					/* If COT successful start call! */
					if (!(e->digittimeout.cot_check_required || e->digittimeout.cot_performed_on_previous_cic) || e->digittimeout.cot_check_passed) {
						ss7_start_call(p, linkset);
					}
				} else {
					ast_debug(1, "Call on CIC for unconfigured extension %s\n", p->exten);
					isup_rel(linkset->ss7, e->digittimeout.call, AST_CAUSE_UNALLOCATED);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_COT:
				if (e->cot.cot_performed_on_previous_cic) {
					chanpos = sig_ss7_find_cic(linkset, (e->cot.cic - 1), e->cot.opc);
					/* some stupid switches do this!!! */
					if (-1 < chanpos) {
						p = linkset->pvts[chanpos];
						sig_ss7_lock_private(p);
						sig_ss7_set_inservice(p, 1);
						sig_ss7_loopback(p, 0);
						sig_ss7_unlock_private(p);;
						ast_verb(1, "Loop turned off on CIC: %d PC: %d\n",  (e->cot.cic - 1), e->cot.opc);
					}
				}

				chanpos = ss7_find_cic_gripe(linkset, e->cot.cic, e->cot.opc, "COT");
				if (chanpos < 0) {
					isup_free_call(ss7, e->cot.call);
					break;
				}
				p = linkset->pvts[chanpos];

				sig_ss7_lock_private(p);
				p->ss7call = e->cot.call;

				if (p->loopedback) {
					sig_ss7_loopback(p, 0);
					ast_verb(1, "Loop turned off on CIC: %d PC: %d\n",  e->cot.cic, e->cot.opc);
				}

				/* Don't start call if we didn't get IAM or COT failed! */
				if ((e->cot.got_sent_msg & ISUP_GOT_IAM) && e->cot.passed && p->called_complete) {
					ss7_start_call(p, linkset);
				}

				p->ss7call = isup_free_call_if_clear(ss7, p->ss7call);
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_CCR:
				ast_debug(1, "Got CCR request on CIC %d\n", e->ccr.cic);
				chanpos = ss7_find_cic_gripe(linkset, e->ccr.cic, e->ccr.opc, "CCR");
				if (chanpos < 0) {
					isup_free_call(ss7, e->ccr.call);
					break;
				}

				p = linkset->pvts[chanpos];

				sig_ss7_lock_private(p);
				p->ss7call = e->ccr.call;
				sig_ss7_loopback(p, 1);
				if (linkset->type == SS7_ANSI) {
					isup_lpa(linkset->ss7, e->ccr.cic, p->dpc);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_CVT:
				ast_debug(1, "Got CVT request on CIC %d\n", e->cvt.cic);
				chanpos = ss7_find_cic_gripe(linkset, e->cvt.cic, e->cvt.opc, "CVT");
				if (chanpos < 0) {
					isup_free_call(ss7, e->cvt.call);
					break;
				}

				p = linkset->pvts[chanpos];

				sig_ss7_lock_private(p);
				p->ss7call = e->cvt.call;
				sig_ss7_loopback(p, 1);
				if (!p->owner) {
					p->ss7call = isup_free_call_if_clear(ss7, e->cvt.call);
				}
				isup_cvr(linkset->ss7, e->cvt.cic, p->dpc);
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_REL:
				chanpos = ss7_find_cic_gripe(linkset, e->rel.cic, e->rel.opc, "REL");
				if (chanpos < 0) {
					isup_free_call(ss7, e->rel.call);
					break;
				}
				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				p->ss7call = e->rel.call;
				callid = func_ss7_linkset_callid(linkset, chanpos);
				sig_ss7_lock_owner(linkset, chanpos);
				if (p->owner) {
					snprintf(cause_str, sizeof(cause_str), "SS7 ISUP_EVENT_REL (%d)", e->rel.cause);
					ss7_queue_pvt_cause_data(p->owner, cause_str, e->rel.cause);

					ast_channel_hangupcause_set(p->owner, e->rel.cause);
					ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_DEV);
					p->do_hangup = SS7_HANGUP_SEND_RLC;
					ast_channel_unlock(p->owner);
				} else {
					ast_verb(1, "REL on CIC %d DPC %d without owner!\n", p->cic, p->dpc);
					isup_rlc(ss7, p->ss7call);
					p->ss7call = isup_free_call_if_clear(ss7, p->ss7call);
				}

				/* End the loopback if we have one */
				sig_ss7_loopback(p, 0);

				/* the rel is not complete here!!! */
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_ACM:
				chanpos = ss7_find_cic_gripe(linkset, e->acm.cic, e->acm.opc, "ACM");
				if (chanpos < 0) {
					isup_free_call(ss7, e->acm.call);
					break;
				}

				p = linkset->pvts[chanpos];

				ast_debug(1, "Queueing frame from SS7_EVENT_ACM on CIC %d\n", p->cic);

				if (e->acm.call_ref_ident > 0) {
					p->rlt = 1; /* Setting it but not using it here*/
				}

				sig_ss7_lock_private(p);
				p->ss7call = e->acm.call;
				callid = func_ss7_linkset_callid(linkset, chanpos);
				sig_ss7_queue_control(linkset, chanpos, AST_CONTROL_PROCEEDING);
				if (p->call_level < SIG_SS7_CALL_LEVEL_PROCEEDING) {
					p->call_level = SIG_SS7_CALL_LEVEL_PROCEEDING;
				}
				sig_ss7_set_dialing(p, 0);
				/* Send alerting if subscriber is free */
				if (e->acm.called_party_status_ind == 1) {
					if (p->call_level < SIG_SS7_CALL_LEVEL_ALERTING) {
						p->call_level = SIG_SS7_CALL_LEVEL_ALERTING;
					}
					sig_ss7_lock_owner(linkset, chanpos);
					if (p->owner) {
						ast_setstate(p->owner, AST_STATE_RINGING);
						ast_channel_unlock(p->owner);
					}
					sig_ss7_queue_control(linkset, chanpos, AST_CONTROL_RINGING);
				}
				p->echocontrol_ind = e->acm.echocontrol_ind;
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_CGB:
				chanpos = ss7_find_cic_gripe(linkset, e->cgb.startcic, e->cgb.opc, "CGB");
				if (chanpos < 0) {
					isup_free_call(ss7, e->cgb.call);
					break;
				}
				p = linkset->pvts[chanpos];
				ss7_check_range(linkset, e->cgb.startcic, e->cgb.endcic,
					e->cgb.opc, e->cgb.status);
				ss7_block_cics(linkset, e->cgb.startcic, e->cgb.endcic,
					e->cgb.opc, e->cgb.status, 1, 1,
					(e->cgb.type) ? SS7_BLOCKED_HARDWARE : SS7_BLOCKED_MAINTENANCE);

				sig_ss7_lock_private(p);
				p->ss7call = e->cgb.call;

				isup_cgba(linkset->ss7, p->ss7call, e->cgb.endcic, e->cgb.status);
				if (!p->owner) {
					p->ss7call = isup_free_call_if_clear(ss7, e->cgb.call);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_CGU:
				chanpos = ss7_find_cic_gripe(linkset, e->cgu.startcic, e->cgu.opc, "CGU");
				if (chanpos < 0) {
					isup_free_call(ss7, e->cgu.call);
					break;
				}
				p = linkset->pvts[chanpos];
				ss7_check_range(linkset, e->cgu.startcic, e->cgu.endcic,
					e->cgu.opc, e->cgu.status);
				ss7_block_cics(linkset, e->cgu.startcic, e->cgu.endcic,
					e->cgu.opc, e->cgu.status, 0, 1,
					e->cgu.type ? SS7_BLOCKED_HARDWARE : SS7_BLOCKED_MAINTENANCE);

				sig_ss7_lock_private(p);
				p->ss7call = e->cgu.call;

				isup_cgua(linkset->ss7, p->ss7call, e->cgu.endcic, e->cgu.status);
				if (!p->owner) {
					p->ss7call = isup_free_call_if_clear(ss7, e->cgu.call);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_UCIC:
				chanpos = ss7_find_cic_gripe(linkset, e->ucic.cic, e->ucic.opc, "UCIC");
				if (chanpos < 0) {
					isup_free_call(ss7, e->ucic.call);
					break;
				}
				p = linkset->pvts[chanpos];
				ast_debug(1, "Unequiped Circuit Id Code on CIC %d\n", e->ucic.cic);
				sig_ss7_lock_private(p);
				sig_ss7_lock_owner(linkset, chanpos);
				if (p->owner) {
					ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_DEV);
					ast_channel_unlock(p->owner);
				}
				sig_ss7_set_remotelyblocked(p, 1, SS7_BLOCKED_MAINTENANCE);
				sig_ss7_set_inservice(p, 0);
				p->ss7call = NULL;
				isup_free_call(ss7, e->ucic.call);
				sig_ss7_unlock_private(p);/* doesn't require a SS7 acknowledgement */
				break;
			case ISUP_EVENT_BLO:
				chanpos = ss7_find_cic_gripe(linkset, e->blo.cic, e->blo.opc, "BLO");
				if (chanpos < 0) {
					isup_free_call(ss7, e->blo.call);
					break;
				}
				p = linkset->pvts[chanpos];
				ast_debug(1, "Blocking CIC %d\n", e->blo.cic);
				sig_ss7_lock_private(p);
				p->ss7call = e->blo.call;
				sig_ss7_set_remotelyblocked(p, 1, SS7_BLOCKED_MAINTENANCE);
				isup_bla(linkset->ss7, e->blo.call);
				sig_ss7_lock_owner(linkset, chanpos);
				if (!p->owner) {
					p->ss7call = isup_free_call_if_clear(ss7, e->blo.call);
				} else {
					if (e->blo.got_sent_msg & ISUP_SENT_IAM) {
						/* Q.784 6.2.2 */
						ast_channel_hangupcause_set(p->owner, SS7_CAUSE_TRY_AGAIN);
					}
					ast_channel_unlock(p->owner);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_BLA:
				chanpos = ss7_find_cic_gripe(linkset, e->bla.cic, e->bla.opc, "BLA");
				if (chanpos < 0) {
					isup_free_call(ss7, e->bla.call);
					break;
				}
				ast_debug(1, "Locally blocking CIC %d\n", e->bla.cic);
				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				p->ss7call = e->bla.call;
				sig_ss7_set_locallyblocked(p, 1, SS7_BLOCKED_MAINTENANCE);
				if (!p->owner) {
					p->ss7call = isup_free_call_if_clear(ss7, p->ss7call);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_UBL:
				chanpos = ss7_find_cic_gripe(linkset, e->ubl.cic, e->ubl.opc, "UBL");
				if (chanpos < 0) {
					isup_free_call(ss7, e->ubl.call);
					break;
				}
				p = linkset->pvts[chanpos];
				ast_debug(1, "Remotely unblocking CIC %d PC %d\n", e->ubl.cic, e->ubl.opc);
				sig_ss7_lock_private(p);
				p->ss7call = e->ubl.call;
				sig_ss7_set_remotelyblocked(p, 0, SS7_BLOCKED_MAINTENANCE);
				isup_uba(linkset->ss7, e->ubl.call);
				if (!p->owner) {
					p->ss7call = isup_free_call_if_clear(ss7, p->ss7call);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_UBA:
				chanpos = ss7_find_cic_gripe(linkset, e->uba.cic, e->uba.opc, "UBA");
				if (chanpos < 0) {
					isup_free_call(ss7, e->uba.call);
					break;
				}
				p = linkset->pvts[chanpos];
				ast_debug(1, "Locally unblocking CIC %d PC %d\n", e->uba.cic, e->uba.opc);
				sig_ss7_lock_private(p);
				p->ss7call = e->uba.call;
				sig_ss7_set_locallyblocked(p, 0, SS7_BLOCKED_MAINTENANCE);
				if (!p->owner) {
					p->ss7call = isup_free_call_if_clear(ss7, p->ss7call);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_CON:
			case ISUP_EVENT_ANM:
				if (e->e == ISUP_EVENT_CON) {
					chanpos = ss7_find_cic_gripe(linkset, e->con.cic, e->con.opc, "CON");
					if (chanpos < 0) {
						isup_free_call(ss7, e->con.call);
						break;
					}
				} else {
					chanpos = ss7_find_cic_gripe(linkset, e->anm.cic, e->anm.opc, "ANM");
					if (chanpos < 0) {
						isup_free_call(ss7, e->anm.call);
						break;
					}
				}

				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				p->ss7call = (e->e == ISUP_EVENT_ANM) ?  e->anm.call : e->con.call;
				callid = func_ss7_linkset_callid(linkset, chanpos);
				if (p->call_level < SIG_SS7_CALL_LEVEL_CONNECT) {
					p->call_level = SIG_SS7_CALL_LEVEL_CONNECT;
				}

				if (!ast_strlen_zero((e->e == ISUP_EVENT_ANM)
					? e->anm.connected_num : e->con.connected_num)) {
					sig_ss7_lock_owner(linkset, chanpos);
					if (p->owner) {
						struct ast_party_connected_line ast_connected;
						char connected_num[AST_MAX_EXTENSION];

						ast_party_connected_line_init(&ast_connected);
						if (e->e == ISUP_EVENT_ANM) {
							ast_connected.id.number.presentation = ss7_pres_scr2cid_pres(
								e->anm.connected_presentation_ind,
								e->anm.connected_screening_ind);
							ss7_apply_plan_to_number(connected_num, sizeof(connected_num),
								linkset, e->anm.connected_num, e->anm.connected_nai);
							ast_connected.id.number.str = ast_strdup(connected_num);
						} else {
							ast_connected.id.number.presentation = ss7_pres_scr2cid_pres(
								e->con.connected_presentation_ind,
								e->con.connected_screening_ind);
							ss7_apply_plan_to_number(connected_num, sizeof(connected_num),
								linkset, e->con.connected_num, e->con.connected_nai);
							ast_connected.id.number.str = ast_strdup(connected_num);
						}
						ast_connected.id.number.valid = 1;
						ast_connected.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;
						ast_channel_queue_connected_line_update(p->owner, &ast_connected, NULL);
						ast_party_connected_line_free(&ast_connected);
						ast_channel_unlock(p->owner);
					}
				}

				sig_ss7_queue_control(linkset, chanpos, AST_CONTROL_ANSWER);
				sig_ss7_set_dialing(p, 0);
				sig_ss7_open_media(p);
				if (((e->e == ISUP_EVENT_ANM) ? !e->anm.echocontrol_ind  :
						!e->con.echocontrol_ind) || !(linkset->flags & LINKSET_FLAG_USEECHOCONTROL)) {
					sig_ss7_set_echocanceller(p, 1);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_RLC:
				chanpos = ss7_find_cic_gripe(linkset, e->rlc.cic, e->rlc.opc, "RLC");
				if (chanpos < 0) {
					isup_free_call(ss7, e->rlc.call);
					break;
				}

				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				p->ss7call = e->rlc.call;
				callid = func_ss7_linkset_callid(linkset, chanpos);
				if (e->rlc.got_sent_msg & (ISUP_SENT_RSC | ISUP_SENT_REL)) {
					sig_ss7_loopback(p, 0);
					if (e->rlc.got_sent_msg & ISUP_SENT_RSC) {
						sig_ss7_set_inservice(p, 1);
					}
				}
				sig_ss7_lock_owner(linkset, chanpos);
				if (!p->owner) {
					p->ss7call = isup_free_call_if_clear(ss7, e->rlc.call);
					p->call_level = SIG_SS7_CALL_LEVEL_IDLE;
				} else {
					p->do_hangup = SS7_HANGUP_DO_NOTHING;
					ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_DEV);
					ast_channel_unlock(p->owner);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_FAA:
				/*!
				 * \todo The handling of the SS7 FAA message is not good and I
				 * don't know enough to handle it correctly.
				 */
				chanpos = ss7_find_cic_gripe(linkset, e->faa.cic, e->faa.opc, "FAA");
				if (chanpos < 0) {
					isup_free_call(ss7, e->faa.call);
					break;
				}

				/* XXX FAR and FAA used for something dealing with transfers? */
				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				callid = func_ss7_linkset_callid(linkset, chanpos);
				ast_debug(1, "FAA received on CIC %d\n", e->faa.cic);
				p->ss7call = isup_free_call_if_clear(ss7, e->faa.call);
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_CGBA:
				chanpos = ss7_find_cic_gripe(linkset, e->cgba.startcic, e->cgba.opc, "CGBA");
				if (chanpos < 0) {	/* Never will be true */
					isup_free_call(ss7, e->cgba.call);
					break;
				}

				ss7_block_cics(linkset, e->cgba.startcic, e->cgba.endcic,
					e->cgba.opc, e->cgba.status, 1, 0,
					e->cgba.type ? SS7_BLOCKED_HARDWARE : SS7_BLOCKED_MAINTENANCE);

				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				p->ss7call = e->cgba.call;

				if (!p->owner) {
					p->ss7call = isup_free_call_if_clear(ss7, p->ss7call);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_CGUA:
				chanpos = ss7_find_cic_gripe(linkset, e->cgua.startcic, e->cgua.opc, "CGUA");
				if (chanpos < 0) { /* Never will be true */
					isup_free_call(ss7, e->cgua.call);
					break;
				}

				ss7_block_cics(linkset, e->cgua.startcic, e->cgua.endcic,
					e->cgua.opc, e->cgua.status, 0, 0,
					e->cgba.type ? SS7_BLOCKED_HARDWARE : SS7_BLOCKED_MAINTENANCE);

				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				p->ss7call = e->cgua.call;

				if (!p->owner) {
					p->ss7call = isup_free_call_if_clear(ss7, p->ss7call);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_SUS:
				chanpos = ss7_find_cic_gripe(linkset, e->sus.cic, e->sus.opc, "SUS");
				if (chanpos < 0) {
					isup_free_call(ss7, e->sus.call);
					break;
				}

				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				p->ss7call = e->sus.call;
				if (!p->owner) {
					p->ss7call = isup_free_call_if_clear(ss7, p->ss7call);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_RES:
				chanpos = ss7_find_cic_gripe(linkset, e->res.cic, e->res.opc, "RES");
				if (chanpos < 0) {
					isup_free_call(ss7, e->res.call);
					break;
				}

				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				p->ss7call = e->res.call;
				if (!p->owner) {
					p->ss7call = isup_free_call_if_clear(ss7, p->ss7call);
				}
				sig_ss7_unlock_private(p);
				break;
			default:
				ast_debug(1, "Unknown event %s\n", ss7_event2str(e->e));
				break;
			}

			/* Call ID stuff needs to be cleaned up here */
			if (callid) {
				ast_callid_threadassoc_remove();
			}
		}
		ast_mutex_unlock(&linkset->lock);
	}

	return 0;
}

static void ss7_rel(struct sig_ss7_linkset *ss7)
{
	/* Release the lock first */
	ast_mutex_unlock(&ss7->lock);

	/* Then break the poll to send our messages */
	if (ss7->master != AST_PTHREADT_NULL) {
		pthread_kill(ss7->master, SIGURG);
	}
}

static void ss7_grab(struct sig_ss7_chan *pvt, struct sig_ss7_linkset *ss7)
{
	/* Grab the lock first */
	while (ast_mutex_trylock(&ss7->lock)) {
		/* Avoid deadlock */
		sig_ss7_deadlock_avoidance_private(pvt);
	}
}

/*!
 * \brief Reset a specific CIC.
 * \since 11.0
 *
 * \param linkset linkset control structure.
 * \param cic Circuit Identification Code
 * \param dpc Destination Point Code
 *
 * \return TRUE on success
 */
int sig_ss7_reset_cic(struct sig_ss7_linkset *linkset, int cic, unsigned int dpc)
{
	int i;

	ast_mutex_lock(&linkset->lock);
	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i] && linkset->pvts[i]->cic == cic && linkset->pvts[i]->dpc == dpc) {
			int res;

			sig_ss7_lock_private(linkset->pvts[i]);
			sig_ss7_set_locallyblocked(linkset->pvts[i], 0, SS7_BLOCKED_MAINTENANCE | SS7_BLOCKED_HARDWARE);
			res = ss7_start_rsc(linkset, i);
			sig_ss7_unlock_private(linkset->pvts[i]);
			ss7_rel(linkset);	/* Also breaks the poll to send our messages */
			return res;
		}
	}
	ss7_rel(linkset);

	return 0;
}

/*!
 * \brief Block or Unblock a specific CIC.
 * \since 11.0
 *
 * \param linkset linkset control structure.
 * \param do_block Action to perform. Block if TRUE.
 * \param which On which CIC to perform the operation.
 *
 * \return 0 on success
 */
int sig_ss7_cic_blocking(struct sig_ss7_linkset *linkset, int do_block, int which)
{
	ast_mutex_lock(&linkset->lock);
	sig_ss7_lock_private(linkset->pvts[which]);
	if (!ss7_find_alloc_call(linkset->pvts[which])) {
		sig_ss7_unlock_private(linkset->pvts[which]);
		ss7_rel(linkset);
		return -1;
	}

	if (do_block) {
		isup_blo(linkset->ss7, linkset->pvts[which]->ss7call);
	} else {
		isup_ubl(linkset->ss7, linkset->pvts[which]->ss7call);
	}

	sig_ss7_unlock_private(linkset->pvts[which]);
	ss7_rel(linkset);	/* Also breaks the poll to send our messages */

	return 0;
}

/*!
 * \brief Block or Unblock a range of CICs.
 * \since 11.0
 *
 * \param linkset linkset control structure.
 * \param do_block Action to perform. Block if TRUE.
 * \param chanpos Channel position to start from.
 * \param endcic Circuit Identification Code of the end of the range.
 * \param state Array of CIC blocking status.
 * \param type Type of the blocking - maintenance or hardware
 *
 * \note Assumes the linkset->lock is already obtained.
 *
 * \return 0 on success
 */
int sig_ss7_group_blocking(struct sig_ss7_linkset *linkset, int do_block, int chanpos, int endcic, unsigned char state[], int type)
{
	sig_ss7_lock_private(linkset->pvts[chanpos]);
	if (!ss7_find_alloc_call(linkset->pvts[chanpos])) {
		sig_ss7_unlock_private(linkset->pvts[chanpos]);
		return -1;
	}

	if (do_block) {
		isup_cgb(linkset->ss7, linkset->pvts[chanpos]->ss7call, endcic, state, type);
	} else {
		isup_cgu(linkset->ss7, linkset->pvts[chanpos]->ss7call, endcic, state, type);
	}

	sig_ss7_unlock_private(linkset->pvts[chanpos]);
	return 0;
}

/*!
 * \brief Reset a group of CICs.
 * \since 11.0
 *
 * \param linkset linkset control structure.
 * \param cic Circuit Identification Code
 * \param dpc Destination Point Code
 * \param range Range of the CICs to reset
 *
 * \note Assumes the linkset->lock is already obtained.
 *
 * \return 0 on success
 */
int sig_ss7_reset_group(struct sig_ss7_linkset *linkset, int cic, unsigned int dpc, int range)
{
	int i;

	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i] && linkset->pvts[i]->cic == cic && linkset->pvts[i]->dpc == dpc) {
			ss7_clear_channels(linkset, cic, cic + range, dpc, SS7_HANGUP_FREE_CALL);
			ss7_block_cics(linkset, cic, cic + range, dpc, NULL, 0, 1,
				SS7_BLOCKED_MAINTENANCE | SS7_BLOCKED_HARDWARE);
			ss7_block_cics(linkset, cic, cic + range, dpc, NULL, 0, 0,
				SS7_BLOCKED_MAINTENANCE | SS7_BLOCKED_HARDWARE);

			sig_ss7_lock_private(linkset->pvts[i]);
			if (!ss7_find_alloc_call(linkset->pvts[i])) {
				sig_ss7_unlock_private(linkset->pvts[i]);
				return -1;
			}
			isup_grs(linkset->ss7, linkset->pvts[i]->ss7call, linkset->pvts[i]->cic + range);
			sig_ss7_unlock_private(linkset->pvts[i]);
			break;
		}
	}
	return 0;
}

void sig_ss7_free_isup_call(struct sig_ss7_linkset *linkset, int channel)
{
	sig_ss7_lock_private(linkset->pvts[channel]);
	if (linkset->pvts[channel]->ss7call) {
		isup_free_call(linkset->ss7, linkset->pvts[channel]->ss7call);
		linkset->pvts[channel]->ss7call = NULL;
	}
	sig_ss7_unlock_private(linkset->pvts[channel]);
}

static int ss7_parse_prefix(struct sig_ss7_chan *p, const char *number, char *nai)
{
	int strip = 0;

	if (strncmp(number, p->ss7->internationalprefix, strlen(p->ss7->internationalprefix)) == 0) {
		strip = strlen(p->ss7->internationalprefix);
		*nai = SS7_NAI_INTERNATIONAL;
	} else if (strncmp(number, p->ss7->nationalprefix, strlen(p->ss7->nationalprefix)) == 0) {
		strip = strlen(p->ss7->nationalprefix);
		*nai = SS7_NAI_NATIONAL;
	} else if (strncmp(number, p->ss7->networkroutedprefix, strlen(p->ss7->networkroutedprefix)) == 0) {
		strip = strlen(p->ss7->networkroutedprefix);
		*nai = SS7_NAI_NETWORKROUTED;
	} else if (strncmp(number, p->ss7->unknownprefix, strlen(p->ss7->unknownprefix)) == 0) {
		strip = strlen(p->ss7->unknownprefix);
		*nai = SS7_NAI_UNKNOWN;
	} else if (strncmp(number, p->ss7->subscriberprefix, strlen(p->ss7->subscriberprefix)) == 0) {
		strip = strlen(p->ss7->subscriberprefix);
		*nai = SS7_NAI_SUBSCRIBER;
	} else {
		*nai = SS7_NAI_SUBSCRIBER;
	}

	return strip;
}

/*!
 * \brief Notify the SS7 layer that the link is in alarm.
 * \since 1.8
 *
 * \param linkset Controlling linkset for the channel.
 * \param which Link index of the signaling channel.
 *
 * \return Nothing
 */
void sig_ss7_link_alarm(struct sig_ss7_linkset *linkset, int which)
{
	linkset->linkstate[which] |= (LINKSTATE_DOWN | LINKSTATE_INALARM);
	linkset->linkstate[which] &= ~LINKSTATE_UP;
	ss7_link_alarm(linkset->ss7, linkset->fds[which]);
}

/*!
 * \brief Notify the SS7 layer that the link is no longer in alarm.
 * \since 1.8
 *
 * \param linkset Controlling linkset for the channel.
 * \param which Link index of the signaling channel.
 *
 * \return Nothing
 */
void sig_ss7_link_noalarm(struct sig_ss7_linkset *linkset, int which)
{
	linkset->linkstate[which] &= ~(LINKSTATE_INALARM | LINKSTATE_DOWN);
	linkset->linkstate[which] |= LINKSTATE_STARTING;
	ss7_link_noalarm(linkset->ss7, linkset->fds[which]);
}

/*!
 * \brief Setup and add a SS7 link channel.
 * \since 1.8
 *
 * \param linkset Controlling linkset for the channel.
 * \param which Link index of the signaling channel.
 * \param ss7type Switch type of the linkset
 * \param transport Signaling transport of channel.
 * \param inalarm Non-zero if the channel is in alarm.
 * \param networkindicator User configuration parameter.
 * \param pointcode User configuration parameter.
 * \param adjpointcode User configuration parameter.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int sig_ss7_add_sigchan(struct sig_ss7_linkset *linkset, int which, int ss7type, int transport, int inalarm, int networkindicator, int pointcode, int adjpointcode, int cur_slc)
{
	if (!linkset->ss7) {
		linkset->type = ss7type;
		linkset->ss7 = ss7_new(ss7type);
		if (!linkset->ss7) {
			ast_log(LOG_ERROR, "Can't create new SS7!\n");
			return -1;
		}
	}

	ss7_set_network_ind(linkset->ss7, networkindicator);
	ss7_set_pc(linkset->ss7, pointcode);

	if (ss7_add_link(linkset->ss7, transport, linkset->fds[which], cur_slc, adjpointcode)) {
		ast_log(LOG_WARNING, "Could not add SS7 link!\n");
	}

	if (inalarm) {
		linkset->linkstate[which] = LINKSTATE_DOWN | LINKSTATE_INALARM;
		ss7_link_alarm(linkset->ss7, linkset->fds[which]);
	} else {
		linkset->linkstate[which] = LINKSTATE_DOWN;
		ss7_link_noalarm(linkset->ss7, linkset->fds[which]);
	}

	return 0;
}

/*!
 * \brief Determine if the specified channel is available for an outgoing call.
 * \since 1.8
 *
 * \param p Signaling private structure pointer.
 *
 * \retval TRUE if the channel is available.
 */
int sig_ss7_available(struct sig_ss7_chan *p)
{
	int available;

	if (!p->ss7) {
		/* Something is wrong here.  A SS7 channel without the ss7 pointer? */
		return 0;
	}

	/* Only have to deal with the linkset lock. */
	ast_mutex_lock(&p->ss7->lock);
	available = sig_ss7_is_chan_available(p);
	if (available) {
		p->ss7call = isup_new_call(p->ss7->ss7, p->cic, p->dpc, 1);
		if (!p->ss7call) {
			ast_log(LOG_ERROR, "Unable to allocate new SS7 call!\n");
			available = 0;
		} else {
			p->call_level = SIG_SS7_CALL_LEVEL_ALLOCATED;
		}
	}
	ast_mutex_unlock(&p->ss7->lock);

	return available;
}

static unsigned char cid_pres2ss7pres(int cid_pres)
{
	 return (cid_pres >> 5) & 0x03;
}

static unsigned char cid_pres2ss7screen(int cid_pres)
{
	return cid_pres & 0x03;
}

static void ss7_connected_line_update(struct sig_ss7_chan *p, struct ast_party_connected_line *connected)
{
	int connected_strip = 0;
	char connected_nai;
	unsigned char connected_pres;
	unsigned char connected_screen;
	const char *connected_num;

	if (!connected->id.number.valid) {
		return;
	}

	connected_num = S_OR(connected->id.number.str, "");
	if (p->ss7->called_nai ==  SS7_NAI_DYNAMIC) {
		connected_strip = ss7_parse_prefix(p, connected_num, &connected_nai);
	} else {
		connected_nai = p->ss7->called_nai;
	}

	connected_pres = cid_pres2ss7pres(connected->id.number.presentation);
	connected_screen = cid_pres2ss7screen(connected->id.number.presentation);

	isup_set_connected(p->ss7call, connected_num + connected_strip, connected_nai, connected_pres, connected_screen);
}

static unsigned char ss7_redirect_reason(struct sig_ss7_chan *p, struct ast_party_redirecting *redirecting, int orig)
{
	int reason = (orig) ? redirecting->orig_reason.code : redirecting->reason.code;

	switch (reason) {
	case AST_REDIRECTING_REASON_USER_BUSY:
		return SS7_REDIRECTING_REASON_USER_BUSY;
	case AST_REDIRECTING_REASON_NO_ANSWER:
		return SS7_REDIRECTING_REASON_NO_ANSWER;
	case AST_REDIRECTING_REASON_UNCONDITIONAL:
		return SS7_REDIRECTING_REASON_UNCONDITIONAL;
	}

	if (orig || reason == AST_REDIRECTING_REASON_UNKNOWN) {
		return SS7_REDIRECTING_REASON_UNKNOWN;
	}

	if (reason == AST_REDIRECTING_REASON_UNAVAILABLE) {
		return SS7_REDIRECTING_REASON_UNAVAILABLE;
	}

	if (reason == AST_REDIRECTING_REASON_DEFLECTION) {
		if (p->call_level > SIG_SS7_CALL_LEVEL_PROCEEDING) {
			return SS7_REDIRECTING_REASON_DEFLECTION_DURING_ALERTING;
		}
		return SS7_REDIRECTING_REASON_DEFLECTION_IMMEDIATE_RESPONSE;
	}

	return SS7_REDIRECTING_REASON_UNKNOWN;
}

static unsigned char ss7_redirect_info_ind(struct ast_channel *ast)
{
	const char *redirect_info_ind;
	struct ast_party_redirecting *redirecting = ast_channel_redirecting(ast);

	redirect_info_ind = pbx_builtin_getvar_helper(ast, "SS7_REDIRECT_INFO_IND");
	if (!ast_strlen_zero(redirect_info_ind)) {
		if (!strcasecmp(redirect_info_ind, "CALL_REROUTED_PRES_ALLOWED")) {
			return SS7_INDICATION_REROUTED_PRES_ALLOWED;
		}
		if (!strcasecmp(redirect_info_ind, "CALL_REROUTED_INFO_RESTRICTED")) {
			return SS7_INDICATION_REROUTED_INFO_RESTRICTED;
		}
		if (!strcasecmp(redirect_info_ind, "CALL_DIVERTED_PRES_ALLOWED")) {
			return SS7_INDICATION_DIVERTED_PRES_ALLOWED;
		}
		if (!strcasecmp(redirect_info_ind, "CALL_DIVERTED_INFO_RESTRICTED")) {
			return SS7_INDICATION_DIVERTED_INFO_RESTRICTED;
		}
		if (!strcasecmp(redirect_info_ind, "CALL_REROUTED_PRES_RESTRICTED")) {
			return SS7_INDICATION_REROUTED_PRES_RESTRICTED;
		}
		if (!strcasecmp(redirect_info_ind, "CALL_DIVERTED_PRES_RESTRICTED")) {
			return SS7_INDICATION_DIVERTED_PRES_RESTRICTED;
		}
		if (!strcasecmp(redirect_info_ind, "SPARE")) {
			return SS7_INDICATION_SPARE;
		}
		return SS7_INDICATION_NO_REDIRECTION;
	}

	if (redirecting->reason.code == AST_REDIRECTING_REASON_DEFLECTION) {
		if ((redirecting->to.number.presentation & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED) {
			if ((redirecting->orig.number.presentation & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED) {
				return SS7_INDICATION_DIVERTED_PRES_ALLOWED;
			}
			return SS7_INDICATION_DIVERTED_PRES_RESTRICTED;
		}
		return SS7_INDICATION_DIVERTED_INFO_RESTRICTED;
	}

	if ((redirecting->to.number.presentation & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED) {
		if ((redirecting->orig.number.presentation & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED) {
			return SS7_INDICATION_REROUTED_PRES_ALLOWED;
		}
		return SS7_INDICATION_REROUTED_PRES_RESTRICTED;
	}
	return SS7_INDICATION_REROUTED_INFO_RESTRICTED;
}

static void ss7_redirecting_update(struct sig_ss7_chan *p, struct ast_channel *ast)
{
	int num_nai_strip = 0;
	struct ast_party_redirecting *redirecting = ast_channel_redirecting(ast);

	if (!redirecting->count) {
		return;
	}

	isup_set_redirect_counter(p->ss7call, redirecting->count);

	if (redirecting->orig.number.valid) {
		char ss7_orig_called_nai = p->ss7->called_nai;
		const char *ss7_orig_called_num = S_OR(redirecting->orig.number.str, "");

		if (ss7_orig_called_nai == SS7_NAI_DYNAMIC) {
			num_nai_strip = ss7_parse_prefix(p, ss7_orig_called_num, &ss7_orig_called_nai);
		} else {
			num_nai_strip = 0;
		}
		isup_set_orig_called_num(p->ss7call, ss7_orig_called_num + num_nai_strip,
			ss7_orig_called_nai,
			cid_pres2ss7pres(redirecting->orig.number.presentation),
			cid_pres2ss7screen(redirecting->orig.number.presentation));
	}

	if (redirecting->from.number.valid) {
		char ss7_redirecting_num_nai = p->ss7->calling_nai;
		const char *redirecting_number = S_OR(redirecting->from.number.str, "");

		if (ss7_redirecting_num_nai == SS7_NAI_DYNAMIC) {
			num_nai_strip = ss7_parse_prefix(p, redirecting_number, &ss7_redirecting_num_nai);
		} else {
			num_nai_strip = 0;
		}

		isup_set_redirecting_number(p->ss7call, redirecting_number + num_nai_strip,
			ss7_redirecting_num_nai,
			cid_pres2ss7pres(redirecting->from.number.presentation),
			cid_pres2ss7screen(redirecting->from.number.presentation));
	}

	isup_set_redirection_info(p->ss7call, ss7_redirect_info_ind(ast),
		ss7_redirect_reason(p, ast_channel_redirecting(ast), 1),
		redirecting->count, ss7_redirect_reason(p, ast_channel_redirecting(ast), 0));
}

/*!
 * \brief Dial out using the specified SS7 channel.
 * \since 1.8
 *
 * \param p Signaling private structure pointer.
 * \param ast Asterisk channel structure pointer.
 * \param rdest Dialstring.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int sig_ss7_call(struct sig_ss7_chan *p, struct ast_channel *ast, const char *rdest)
{
	char ss7_called_nai;
	int called_nai_strip;
	char ss7_calling_nai;
	int calling_nai_strip;
	const char *col_req = NULL;
	const char *ss7_cug_indicator_str;
	const char *ss7_cug_interlock_ni;
	const char *ss7_cug_interlock_code;
	const char *ss7_interworking_indicator;
	const char *ss7_forward_indicator_pmbits;
	unsigned char ss7_cug_indicator;
	const char *charge_str = NULL;
	const char *gen_address = NULL;
	const char *gen_digits = NULL;
	const char *gen_dig_type = NULL;
	const char *gen_dig_scheme = NULL;
	const char *gen_name = NULL;
	const char *jip_digits = NULL;
	const char *lspi_ident = NULL;
	const char *rlt_flag = NULL;
	const char *call_ref_id = NULL;
	const char *call_ref_pc = NULL;
	const char *send_far = NULL;
	const char *tmr = NULL;
	char *c;
	char *l;
	char dest[256];

	ast_copy_string(dest, rdest, sizeof(dest));

	c = strchr(dest, '/');
	if (c) {
		c++;
	} else {
		c = "";
	}
	if (strlen(c) < p->stripmsd) {
		ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
		return -1;
	}

	if (!p->hidecallerid) {
		l = ast_channel_connected(ast)->id.number.valid ? ast_channel_connected(ast)->id.number.str : NULL;
	} else {
		l = NULL;
	}

	ss7_grab(p, p->ss7);

	if (p->call_level != SIG_SS7_CALL_LEVEL_ALLOCATED) {
		/* Call collision before sending IAM.  Abort call. */
		ss7_rel(p->ss7);
		return -1;
	}

	called_nai_strip = 0;
	ss7_called_nai = p->ss7->called_nai;
	if (ss7_called_nai == SS7_NAI_DYNAMIC) { /* compute dynamically */
		called_nai_strip = ss7_parse_prefix(p, c + p->stripmsd, &ss7_called_nai);
	}
	isup_set_called(p->ss7call, c + p->stripmsd + called_nai_strip, ss7_called_nai, p->ss7->ss7);

	calling_nai_strip = 0;
	ss7_calling_nai = p->ss7->calling_nai;
	if ((l != NULL) && (ss7_calling_nai == SS7_NAI_DYNAMIC)) { /* compute dynamically */
		calling_nai_strip = ss7_parse_prefix(p, l, &ss7_calling_nai);
	}

	isup_set_calling(p->ss7call, l ? (l + calling_nai_strip) : NULL, ss7_calling_nai,
		p->use_callingpres ? cid_pres2ss7pres(ast_channel_connected(ast)->id.number.presentation)
			: (l ? SS7_PRESENTATION_ALLOWED
				: (ast_channel_connected(ast)->id.number.presentation == AST_PRES_UNAVAILABLE
					? SS7_PRESENTATION_ADDR_NOT_AVAILABLE : SS7_PRESENTATION_RESTRICTED)),
		p->use_callingpres ? cid_pres2ss7screen(ast_channel_connected(ast)->id.number.presentation) : SS7_SCREENING_USER_PROVIDED);

	isup_set_oli(p->ss7call, ast_channel_connected(ast)->ani2);

	/* Set the charge number if it is set */
	charge_str = pbx_builtin_getvar_helper(ast, "SS7_CHARGE_NUMBER");
	if (charge_str)
		isup_set_charge(p->ss7call, charge_str, SS7_ANI_CALLING_PARTY_SUB_NUMBER, 0x10);

	gen_address = pbx_builtin_getvar_helper(ast, "SS7_GENERIC_ADDRESS");
	if (gen_address)
		isup_set_gen_address(p->ss7call, gen_address, p->gen_add_nai,p->gen_add_pres_ind, p->gen_add_num_plan,p->gen_add_type); /* need to add some types here for NAI,PRES,TYPE */

	gen_digits = pbx_builtin_getvar_helper(ast, "SS7_GENERIC_DIGITS");
	gen_dig_type = pbx_builtin_getvar_helper(ast, "SS7_GENERIC_DIGTYPE");
	gen_dig_scheme = pbx_builtin_getvar_helper(ast, "SS7_GENERIC_DIGSCHEME");
	if (gen_digits)
		isup_set_gen_digits(p->ss7call, gen_digits, atoi(gen_dig_type), atoi(gen_dig_scheme));

	gen_name = pbx_builtin_getvar_helper(ast, "SS7_GENERIC_NAME");
	if (gen_name)
		isup_set_generic_name(p->ss7call, gen_name, GEN_NAME_TYPE_CALLING_NAME, GEN_NAME_AVAIL_AVAILABLE, GEN_NAME_PRES_ALLOWED);

	jip_digits = pbx_builtin_getvar_helper(ast, "SS7_JIP");
	if (jip_digits)
		isup_set_jip_digits(p->ss7call, jip_digits);

	lspi_ident = pbx_builtin_getvar_helper(ast, "SS7_LSPI_IDENT");
	if (lspi_ident)
		isup_set_lspi(p->ss7call, lspi_ident, 0x18, 0x7, 0x00);

	rlt_flag = pbx_builtin_getvar_helper(ast, "SS7_RLT_ON");
	if ((rlt_flag) && ((strncmp("NO", rlt_flag, strlen(rlt_flag))) != 0 )) {
		isup_set_lspi(p->ss7call, rlt_flag, 0x18, 0x7, 0x00); /* Setting for Nortel DMS-250/500 */
	}

	call_ref_id = pbx_builtin_getvar_helper(ast, "SS7_CALLREF_IDENT");
	call_ref_pc = pbx_builtin_getvar_helper(ast, "SS7_CALLREF_PC");
	if (call_ref_id && call_ref_pc) {
		isup_set_callref(p->ss7call, atoi(call_ref_id),
				 call_ref_pc ? atoi(call_ref_pc) : 0);
	}

	send_far = pbx_builtin_getvar_helper(ast, "SS7_SEND_FAR");
	if (send_far && strncmp("NO", send_far, strlen(send_far)) != 0) {
		isup_far(p->ss7->ss7, p->ss7call);
	}

	tmr = pbx_builtin_getvar_helper(ast, "SS7_TMR_NUM");
	if (tmr) {
		isup_set_tmr(p->ss7call, atoi(tmr));
	} else if ((tmr = pbx_builtin_getvar_helper(ast, "SS7_TMR")) && tmr[0] != '\0') {
		if (!strcasecmp(tmr, "SPEECH")) {
			isup_set_tmr(p->ss7call, SS7_TMR_SPEECH);
		} else if (!strcasecmp(tmr, "SPARE")) {
			isup_set_tmr(p->ss7call, SS7_TMR_SPARE);
		} else if (!strcasecmp(tmr, "3K1_AUDIO")) {
			isup_set_tmr(p->ss7call, SS7_TMR_3K1_AUDIO);
		} else if (!strcasecmp(tmr, "64K_UNRESTRICTED")) {
			isup_set_tmr(p->ss7call, SS7_TMR_64K_UNRESTRICTED);
		} else {
			isup_set_tmr(p->ss7call, SS7_TMR_N64K_OR_SPARE);
		}
	}

	col_req = pbx_builtin_getvar_helper(ast, "SS7_COL_REQUEST");
	if (ast_true(col_req)) {
		isup_set_col_req(p->ss7call);
	}

	ss7_cug_indicator_str = pbx_builtin_getvar_helper(ast, "SS7_CUG_INDICATOR");
	if (!ast_strlen_zero(ss7_cug_indicator_str)) {
		if (!strcasecmp(ss7_cug_indicator_str, "OUTGOING_ALLOWED")) {
			ss7_cug_indicator = ISUP_CUG_OUTGOING_ALLOWED;
		} else if (!strcasecmp(ss7_cug_indicator_str, "OUTGOING_NOT_ALLOWED")) {
			ss7_cug_indicator = ISUP_CUG_OUTGOING_NOT_ALLOWED;
		} else {
			ss7_cug_indicator = ISUP_CUG_NON;
		}

		if (ss7_cug_indicator != ISUP_CUG_NON) {
			ss7_cug_interlock_code = pbx_builtin_getvar_helper(ast, "SS7_CUG_INTERLOCK_CODE");
			ss7_cug_interlock_ni = pbx_builtin_getvar_helper(ast, "SS7_CUG_INTERLOCK_NI");
			if (ss7_cug_interlock_code && ss7_cug_interlock_ni && strlen(ss7_cug_interlock_ni) == 4) {
				isup_set_cug(p->ss7call, ss7_cug_indicator, ss7_cug_interlock_ni, atoi(ss7_cug_interlock_code));
			}
		}
	}

	ss7_redirecting_update(p, ast);

	isup_set_echocontrol(p->ss7call, (p->ss7->flags & LINKSET_FLAG_DEFAULTECHOCONTROL) ? 1 : 0);
	ss7_interworking_indicator = pbx_builtin_getvar_helper(ast, "SS7_INTERWORKING_INDICATOR");
	if (ss7_interworking_indicator) {
		isup_set_interworking_indicator(p->ss7call, ast_true(ss7_interworking_indicator));
	}

	ss7_forward_indicator_pmbits = pbx_builtin_getvar_helper(ast, "SS7_FORWARD_INDICATOR_PMBITS");
	if (ss7_forward_indicator_pmbits) {
		isup_set_forward_indicator_pmbits(p->ss7call, atoi(ss7_forward_indicator_pmbits));
	}

	p->call_level = SIG_SS7_CALL_LEVEL_SETUP;
	p->do_hangup = SS7_HANGUP_SEND_REL;
	isup_iam(p->ss7->ss7, p->ss7call);
	sig_ss7_set_dialing(p, 1);
	ast_setstate(ast, AST_STATE_DIALING);
	ss7_rel(p->ss7);
	return 0;
}

/*!
 * \brief SS7 hangup channel.
 * \since 1.8
 *
 * \param p Signaling private structure pointer.
 * \param ast Asterisk channel structure pointer.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int sig_ss7_hangup(struct sig_ss7_chan *p, struct ast_channel *ast)
{
	if (!ast_channel_tech_pvt(ast)) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}

	p->owner = NULL;
	sig_ss7_set_dialing(p, 0);
	sig_ss7_set_outgoing(p, 0);
	p->progress = 0;
	p->rlt = 0;
	p->exten[0] = '\0';
	/* Perform low level hangup if no owner left */
	ss7_grab(p, p->ss7);
	p->call_level = SIG_SS7_CALL_LEVEL_IDLE;
	if (p->ss7call) {
		switch (p->do_hangup) {
		case SS7_HANGUP_SEND_REL:
			{
				const char *cause = pbx_builtin_getvar_helper(ast,"SS7_CAUSE");
				int icause = ast_channel_hangupcause(ast) ? ast_channel_hangupcause(ast) : -1;

				if (cause) {
					if (atoi(cause)) {
						icause = atoi(cause);
					}
				}
				if (icause > 255) {
					icause = 16;
				}

				isup_rel(p->ss7->ss7, p->ss7call, icause);
				p->do_hangup = SS7_HANGUP_DO_NOTHING;
			}
			break;
		case SS7_HANGUP_SEND_RSC:
			ss7_do_rsc(p);
			p->do_hangup = SS7_HANGUP_DO_NOTHING;
			break;
		case SS7_HANGUP_SEND_RLC:
			isup_rlc(p->ss7->ss7, p->ss7call);
			p->do_hangup = SS7_HANGUP_DO_NOTHING;
			p->ss7call = isup_free_call_if_clear(p->ss7->ss7, p->ss7call);
			break;
		case SS7_HANGUP_FREE_CALL:
			p->do_hangup = SS7_HANGUP_DO_NOTHING;
			isup_free_call(p->ss7->ss7, p->ss7call);
			p->ss7call = NULL;
			break;
		case SS7_HANGUP_REEVENT_IAM:
			isup_event_iam(p->ss7->ss7, p->ss7call, p->dpc);
			p->do_hangup = SS7_HANGUP_SEND_REL;
			break;
		case SS7_HANGUP_DO_NOTHING:
			p->ss7call = isup_free_call_if_clear(p->ss7->ss7, p->ss7call);
			break;
		}
	}
	ss7_rel(p->ss7);

	return 0;
}

/*!
 * \brief SS7 answer channel.
 * \since 1.8
 *
 * \param p Signaling private structure pointer.
 * \param ast Asterisk channel structure pointer.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int sig_ss7_answer(struct sig_ss7_chan *p, struct ast_channel *ast)
{
	int res;

	ss7_grab(p, p->ss7);
	if (p->call_level < SIG_SS7_CALL_LEVEL_CONNECT) {
		if (p->call_level < SIG_SS7_CALL_LEVEL_PROCEEDING && (p->ss7->flags & LINKSET_FLAG_AUTOACM)) {
			isup_acm(p->ss7->ss7, p->ss7call);
		}
		p->call_level = SIG_SS7_CALL_LEVEL_CONNECT;
	}

	res = isup_anm(p->ss7->ss7, p->ss7call);
	sig_ss7_open_media(p);
	ss7_rel(p->ss7);
	return res;
}

/*!
 * \brief Fix up a channel:  If a channel is consumed, this is called.  Basically update any ->owner links.
 * \since 1.8
 *
 * \param oldchan Old channel pointer to replace.
 * \param newchan New channel pointer to set.
 * \param pchan Signaling private structure pointer.
 *
 * \return Nothing
 */
void sig_ss7_fixup(struct ast_channel *oldchan, struct ast_channel *newchan, struct sig_ss7_chan *pchan)
{
	if (pchan->owner == oldchan) {
		pchan->owner = newchan;
	}
}

/*!
 * \brief SS7 indication.
 * \since 1.8
 *
 * \param p Signaling private structure pointer.
 * \param chan Asterisk channel structure pointer.
 * \param condition AST control frame subtype.
 * \param data AST control frame payload contents.
 * \param datalen Length of payload contents.
 *
 * \retval 0 on success.
 * \retval -1 on error or indication condition not handled.
 */
int sig_ss7_indicate(struct sig_ss7_chan *p, struct ast_channel *chan, int condition, const void *data, size_t datalen)
{
	int res = -1;

	switch (condition) {
	case AST_CONTROL_BUSY:
		if (p->call_level < SIG_SS7_CALL_LEVEL_CONNECT) {
			ast_channel_hangupcause_set(chan, AST_CAUSE_USER_BUSY);
			ast_softhangup_nolock(chan, AST_SOFTHANGUP_DEV);
			res = 0;
			break;
		}
		res = sig_ss7_play_tone(p, SIG_SS7_TONE_BUSY);
		break;
	case AST_CONTROL_RINGING:
		ss7_grab(p, p->ss7);
		if (p->call_level < SIG_SS7_CALL_LEVEL_ALERTING && !p->outgoing) {
			if ((isup_far(p->ss7->ss7, p->ss7call)) != -1) {
				p->rlt = 1;
			}

			if (p->call_level < SIG_SS7_CALL_LEVEL_PROCEEDING && (p->ss7->flags & LINKSET_FLAG_AUTOACM)) {
				isup_acm(p->ss7->ss7, p->ss7call);
			}

			/* No need to send CPG if call will be RELEASE */
			if (p->rlt != 1) {
				isup_cpg(p->ss7->ss7, p->ss7call, CPG_EVENT_ALERTING);
			}

			p->call_level = SIG_SS7_CALL_LEVEL_ALERTING;
		}
		ss7_rel(p->ss7);

		res = sig_ss7_play_tone(p, SIG_SS7_TONE_RINGTONE);

		if (ast_channel_state(chan) != AST_STATE_UP && ast_channel_state(chan) != AST_STATE_RING) {
			ast_setstate(chan, AST_STATE_RINGING);
		}
		break;
	case AST_CONTROL_PROCEEDING:
		ast_debug(1,"Received AST_CONTROL_PROCEEDING on %s\n",ast_channel_name(chan));
		ss7_grab(p, p->ss7);
		/* This IF sends the FAR for an answered ALEG call */
		if (ast_channel_state(chan) == AST_STATE_UP && (p->rlt != 1)){
			if ((isup_far(p->ss7->ss7, p->ss7call)) != -1) {
				p->rlt = 1;
			}
		}

		if (p->call_level < SIG_SS7_CALL_LEVEL_PROCEEDING && !p->outgoing) {
			p->call_level = SIG_SS7_CALL_LEVEL_PROCEEDING;
			isup_acm(p->ss7->ss7, p->ss7call);
		}
		ss7_rel(p->ss7);
		/* don't continue in ast_indicate */
		res = 0;
		break;
	case AST_CONTROL_PROGRESS:
		ast_debug(1,"Received AST_CONTROL_PROGRESS on %s\n",ast_channel_name(chan));
		ss7_grab(p, p->ss7);
		if (!p->progress && p->call_level < SIG_SS7_CALL_LEVEL_ALERTING && !p->outgoing) {
			p->progress = 1;	/* No need to send inband-information progress again. */
			isup_cpg(p->ss7->ss7, p->ss7call, CPG_EVENT_INBANDINFO);

			/* enable echo canceler here on SS7 calls */
			if (!p->echocontrol_ind || !(p->ss7->flags & LINKSET_FLAG_USEECHOCONTROL)) {
				sig_ss7_set_echocanceller(p, 1);
			}
		}
		ss7_rel(p->ss7);
		/* don't continue in ast_indicate */
		res = 0;
		break;
	case AST_CONTROL_INCOMPLETE:
		if (p->call_level < SIG_SS7_CALL_LEVEL_CONNECT) {
			ast_channel_hangupcause_set(chan, AST_CAUSE_INVALID_NUMBER_FORMAT);
			ast_softhangup_nolock(chan, AST_SOFTHANGUP_DEV);
			res = 0;
			break;
		}
		/* Wait for DTMF digits to complete the dialed number. */
		res = 0;
		break;
	case AST_CONTROL_CONGESTION:
		if (p->call_level < SIG_SS7_CALL_LEVEL_CONNECT) {
			ast_channel_hangupcause_set(chan, AST_CAUSE_CONGESTION);
			ast_softhangup_nolock(chan, AST_SOFTHANGUP_DEV);
			res = 0;
			break;
		}
		res = sig_ss7_play_tone(p, SIG_SS7_TONE_CONGESTION);
		break;
	case AST_CONTROL_HOLD:
		ast_moh_start(chan, data, p->mohinterpret);
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(chan);
		break;
	case AST_CONTROL_SRCUPDATE:
		res = 0;
		break;
	case AST_CONTROL_CONNECTED_LINE:
		ss7_connected_line_update(p, ast_channel_connected(chan));
		res = 0;
		break;
	case AST_CONTROL_REDIRECTING:
		ss7_redirecting_update(p, chan);
		res = 0;
		break;
	case -1:
		res = sig_ss7_play_tone(p, -1);
		break;
	}
	return res;
}

/*!
 * \brief SS7 channel request.
 * \since 1.8
 *
 * \param p Signaling private structure pointer.
 * \param law Companding law preferred
 * \param requestor Asterisk channel requesting a channel to dial (Can be NULL)
 * \param transfercapability
 *
 * \retval ast_channel on success.
 * \retval NULL on error.
 */
struct ast_channel *sig_ss7_request(struct sig_ss7_chan *p, enum sig_ss7_law law,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor,
	int transfercapability)
{
	struct ast_channel *ast;

	/* Companding law is determined by SS7 signaling type. */
	if (p->ss7->type == SS7_ITU) {
		law = SIG_SS7_ALAW;
	} else {
		law = SIG_SS7_ULAW;
	}

	sig_ss7_set_outgoing(p, 1);
	ast = sig_ss7_new_ast_channel(p, AST_STATE_RESERVED, law, transfercapability,
		p->exten, assignedids, requestor);
	if (!ast) {
		sig_ss7_set_outgoing(p, 0);

		/* Release the allocated channel.  Only have to deal with the linkset lock. */
		ast_mutex_lock(&p->ss7->lock);
		p->call_level = SIG_SS7_CALL_LEVEL_IDLE;
		isup_free_call(p->ss7->ss7, p->ss7call);
		ast_mutex_unlock(&p->ss7->lock);
	}
	return ast;
}

/*!
 * \brief Delete the sig_ss7 private channel structure.
 * \since 1.8
 *
 * \param doomed sig_ss7 private channel structure to delete.
 *
 * \return Nothing
 */
void sig_ss7_chan_delete(struct sig_ss7_chan *doomed)
{
	ast_free(doomed);
}

#define SIG_SS7_SC_HEADER	"%-4s %4s %-4s %-3s %-3s %-10s %-4s %s\n"
#define SIG_SS7_SC_LINE		 "%4d %4d %-4s %-3s %-3s %-10s %-4s %s"
void sig_ss7_cli_show_channels_header(int fd)
{
	ast_cli(fd, SIG_SS7_SC_HEADER, "link", "",     "Chan", "Lcl", "Rem", "Call",  "SS7",  "Channel");
	ast_cli(fd, SIG_SS7_SC_HEADER, "set",  "Chan", "Idle", "Blk", "Blk", "Level", "Call", "Name");
}

void sig_ss7_cli_show_channels(int fd, struct sig_ss7_linkset *linkset)
{
	char line[256];
	int idx;
	struct sig_ss7_chan *pvt;

	ast_mutex_lock(&linkset->lock);
	for (idx = 0; idx < linkset->numchans; ++idx) {
		if (!linkset->pvts[idx]) {
			continue;
		}
		pvt = linkset->pvts[idx];
		sig_ss7_lock_private(pvt);
		sig_ss7_lock_owner(linkset, idx);

		snprintf(line, sizeof(line), SIG_SS7_SC_LINE,
			linkset->span,
			pvt->channel,
			sig_ss7_is_chan_available(pvt) ? "Yes" : "No",
			pvt->locallyblocked ? "Yes" : "No",
			pvt->remotelyblocked ? "Yes" : "No",
			sig_ss7_call_level2str(pvt->call_level),
			pvt->ss7call ? "Yes" : "No",
			pvt->owner ? ast_channel_name(pvt->owner) : "");

		if (pvt->owner) {
			ast_channel_unlock(pvt->owner);
		}
		sig_ss7_unlock_private(pvt);

		ast_mutex_unlock(&linkset->lock);
		ast_cli(fd, "%s\n", line);
		ast_mutex_lock(&linkset->lock);
	}
	ast_mutex_unlock(&linkset->lock);
}

/*!
 * \brief Create a new sig_ss7 private channel structure.
 * \since 1.8
 *
 * \param pvt_data Upper layer private data structure.
 * \param ss7 Controlling linkset for the channel.
 *
 * \retval sig_ss7_chan on success.
 * \retval NULL on error.
 */
struct sig_ss7_chan *sig_ss7_chan_new(void *pvt_data, struct sig_ss7_linkset *ss7)
{
	struct sig_ss7_chan *pvt;

	pvt = ast_calloc(1, sizeof(*pvt));
	if (!pvt) {
		return pvt;
	}

	pvt->chan_pvt = pvt_data;
	pvt->ss7 = ss7;

	return pvt;
}

/*!
 * \brief Initialize the SS7 linkset control.
 * \since 1.8
 *
 * \param ss7 SS7 linkset control structure.
 *
 * \return Nothing
 */
void sig_ss7_init_linkset(struct sig_ss7_linkset *ss7)
{
	int idx;

	memset(ss7, 0, sizeof(*ss7));

	ast_mutex_init(&ss7->lock);

	ss7->master = AST_PTHREADT_NULL;
	for (idx = 0; idx < ARRAY_LEN(ss7->fds); ++idx) {
		ss7->fds[idx] = -1;
	}
}

/* ------------------------------------------------------------------- */

#endif	/* defined(HAVE_SS7) */
/* end sig_ss7.c */
