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
#include "asterisk/transcap.h"

#include "sig_ss7.h"
#if defined(LIBSS7_ABI_COMPATIBILITY)
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
	case SIG_SS7_CALL_LEVEL_GLARE:
		return "Glare";
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
	if (sig_ss7_callbacks.set_inservice) {
		sig_ss7_callbacks.set_inservice(p->chan_pvt, is_inservice);
	}
}

static void sig_ss7_set_locallyblocked(struct sig_ss7_chan *p, int is_blocked)
{
	p->locallyblocked = is_blocked;
	if (sig_ss7_callbacks.set_locallyblocked) {
		sig_ss7_callbacks.set_locallyblocked(p->chan_pvt, is_blocked);
	}
}

static void sig_ss7_set_remotelyblocked(struct sig_ss7_chan *p, int is_blocked)
{
	p->remotelyblocked = is_blocked;
	if (sig_ss7_callbacks.set_remotelyblocked) {
		sig_ss7_callbacks.set_remotelyblocked(p->chan_pvt, is_blocked);
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

static struct ast_channel *sig_ss7_new_ast_channel(struct sig_ss7_chan *p, int state, int ulaw, int transfercapability, char *exten, const struct ast_channel *requestor)
{
	struct ast_channel *ast;

	if (sig_ss7_callbacks.new_ast_channel) {
		ast = sig_ss7_callbacks.new_ast_channel(p->chan_pvt, state, ulaw, exten, requestor);
	} else {
		return NULL;
	}
	if (!ast) {
		return NULL;
	}

	if (!p->owner) {
		p->owner = ast;
	}
	p->alreadyhungup = 0;
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
	if (!pvt->inalarm && !pvt->owner && !pvt->ss7call
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
 * \since 11
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
	cause_code->ast_cause = ast_cause;
	ast_copy_string(cause_code->chan_name, ast_channel_name(owner), AST_CHANNEL_NAME);
	ast_copy_string(cause_code->code, cause, datalen + 1 - sizeof(*cause_code));
	ast_queue_control_data(owner, AST_CONTROL_PVT_CAUSE_CODE, cause_code, datalen);
	ast_channel_hangupcause_hash_set(owner, cause_code, datalen);
}


/*!
 * \internal
 * \brief Find the channel position by CIC/DPC.
 *
 * \param linkset SS7 linkset control structure.
 * \param cic Circuit Identification Code
 * \param dpc Destination Point Code
 *
 * \retval chanpos on success.
 * \retval -1 on error.
 */
static int ss7_find_cic(struct sig_ss7_linkset *linkset, int cic, unsigned int dpc)
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

	chanpos = ss7_find_cic(linkset, cic, dpc);
	if (chanpos < 0) {
		ast_log(LOG_WARNING, "Linkset %d: SS7 %s requested unconfigured CIC/DPC %d/%d.\n",
			linkset->span, msg_name, cic, dpc);
		return -1;
	}
	return chanpos;
}

static void ss7_handle_cqm(struct sig_ss7_linkset *linkset, int startcic, int endcic, unsigned int dpc)
{
	unsigned char status[32];
	struct sig_ss7_chan *p = NULL;
	int i, offset;

	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i] && (linkset->pvts[i]->dpc == dpc && ((linkset->pvts[i]->cic >= startcic) && (linkset->pvts[i]->cic <= endcic)))) {
			p = linkset->pvts[i];
			offset = p->cic - startcic;
			status[offset] = 0;
			if (p->locallyblocked)
				status[offset] |= (1 << 0) | (1 << 4);
			if (p->remotelyblocked)
				status[offset] |= (1 << 1) | (1 << 5);
			if (p->ss7call) {
				if (p->outgoing)
					status[offset] |= (1 << 3);
				else
					status[offset] |= (1 << 2);
			} else
				status[offset] |= 0x3 << 2;
		}
	}

	if (p)
		isup_cqr(linkset->ss7, startcic, endcic, dpc, status);
	else
		ast_log(LOG_WARNING, "Could not find any equipped circuits within CQM CICs\n");

}

static inline void ss7_hangup_cics(struct sig_ss7_linkset *linkset, int startcic, int endcic, unsigned int dpc)
{
	int i;

	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i] && (linkset->pvts[i]->dpc == dpc && ((linkset->pvts[i]->cic >= startcic) && (linkset->pvts[i]->cic <= endcic)))) {
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

static inline void ss7_block_cics(struct sig_ss7_linkset *linkset, int startcic, int endcic, unsigned int dpc, unsigned char state[], int block)
{
	int i;

	/* XXX the use of state here seems questionable about matching up with the linkset channels */
	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i] && (linkset->pvts[i]->dpc == dpc && ((linkset->pvts[i]->cic >= startcic) && (linkset->pvts[i]->cic <= endcic)))) {
			if (state) {
				if (state[i])
					sig_ss7_set_remotelyblocked(linkset->pvts[i], block);
			} else
				sig_ss7_set_remotelyblocked(linkset->pvts[i], block);
		}
	}
}

static void ss7_inservice(struct sig_ss7_linkset *linkset, int startcic, int endcic, unsigned int dpc)
{
	int i;

	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i] && (linkset->pvts[i]->dpc == dpc && ((linkset->pvts[i]->cic >= startcic) && (linkset->pvts[i]->cic <= endcic))))
			sig_ss7_set_inservice(linkset->pvts[i], 1);
	}
}

static void ss7_reset_linkset(struct sig_ss7_linkset *linkset)
{
	int i, startcic = -1, endcic, dpc;

	if (linkset->numchans <= 0)
		return;

	startcic = linkset->pvts[0]->cic;
	/* DB: CIC's DPC fix */
	dpc = linkset->pvts[0]->dpc;

	for (i = 0; i < linkset->numchans; i++) {
		if (linkset->pvts[i+1] && linkset->pvts[i+1]->dpc == dpc && ((linkset->pvts[i+1]->cic - linkset->pvts[i]->cic) == 1) && (linkset->pvts[i]->cic - startcic < 31)) {
			continue;
		} else {
			endcic = linkset->pvts[i]->cic;
			ast_verbose("Resetting CICs %d to %d\n", startcic, endcic);
			isup_grs(linkset->ss7, startcic, endcic, dpc);

			/* DB: CIC's DPC fix */
			if (linkset->pvts[i+1]) {
				startcic = linkset->pvts[i+1]->cic;
				dpc = linkset->pvts[i+1]->dpc;
			}
		}
	}
}

/* This function is assumed to be called with the private channel lock and linkset lock held */
static void ss7_start_call(struct sig_ss7_chan *p, struct sig_ss7_linkset *linkset)
{
	struct ss7 *ss7 = linkset->ss7;
	int law;
	struct ast_channel *c;
	char tmp[256];
	struct ast_callid *callid = NULL;
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

	/*
	 * Release the SS7 lock while we create the channel so other
	 * threads can send messages.  We must also release the private
	 * lock to prevent deadlock while creating the channel.
	 */
	ast_mutex_unlock(&linkset->lock);
	sig_ss7_unlock_private(p);
	c = sig_ss7_new_ast_channel(p, AST_STATE_RING, law, 0, p->exten, NULL);
	if (!c) {
		ast_log(LOG_WARNING, "Unable to start PBX on CIC %d\n", p->cic);
		ast_mutex_lock(&linkset->lock);
		sig_ss7_lock_private(p);
		isup_rel(linkset->ss7, p->ss7call, AST_CAUSE_SWITCH_CONGESTION);
		p->call_level = SIG_SS7_CALL_LEVEL_IDLE;
		p->alreadyhungup = 1;
		ast_callid_threadstorage_auto_clean(callid, callid_created);
		return;
	}

	/* Hold the channel and private lock while we setup the channel. */
	ast_channel_lock(c);
	sig_ss7_lock_private(p);

	sig_ss7_set_echocanceller(p, 1);

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
	if (!ast_strlen_zero(p->orig_called_num)) {
		pbx_builtin_setvar_helper(c, "SS7_ORIG_CALLED_NUM", p->orig_called_num);
		/* Clear this after we set it */
		p->orig_called_num[0] = 0;
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

	if (!ast_strlen_zero(p->redirecting_num)) {
		pbx_builtin_setvar_helper(c, "SS7_REDIRECTING_NUMBER", p->redirecting_num);
		/* Clear this after we set it */
		p->redirecting_num[0] = 0;
	}
	if (!ast_strlen_zero(p->generic_name)) {
		pbx_builtin_setvar_helper(c, "SS7_GENERIC_NAME", p->generic_name);
		/* Clear this after we set it */
		p->generic_name[0] = 0;
	}

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
 * \return a reference to the callid bound to the channel which has also
 *         been bound to threadstorage if it exists. If this returns non-NULL,
 *         the callid must be unreffed and the threadstorage should be unbound
 *         before the while loop wraps in ss7_linkset.
 */
static struct ast_callid *func_ss7_linkset_callid(struct sig_ss7_linkset *linkset, int chanpos)
{
	struct ast_callid *callid = NULL;
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
	int nextms = 0;

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

		if ((res < 0) && (errno != EINTR)) {
			ast_log(LOG_ERROR, "poll(%s)\n", strerror(errno));
		} else if (!res) {
			ast_mutex_lock(&linkset->lock);
			ss7_schedule_run(ss7);
			ast_mutex_unlock(&linkset->lock);
			continue;
		}

		ast_mutex_lock(&linkset->lock);
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
			struct ast_callid *callid = NULL;
			int chanpos = -1;
			char cause_str[30];

			if (linkset->debug) {
				ast_verbose("Linkset %d: Processing event: %s\n",
					linkset->span, ss7_event2str(e->e));
			}

			switch (e->e) {
			case SS7_EVENT_UP:
				if (linkset->state != LINKSET_STATE_UP) {
					ast_verbose("--- SS7 Up ---\n");
					ss7_reset_linkset(linkset);
				}
				linkset->state = LINKSET_STATE_UP;
				break;
			case SS7_EVENT_DOWN:
				ast_verbose("--- SS7 Down ---\n");
				linkset->state = LINKSET_STATE_DOWN;
				for (i = 0; i < linkset->numchans; i++) {
					p = linkset->pvts[i];
					if (p) {
						sig_ss7_set_alarm(p, 1);
					}
				}
				break;
			case MTP2_LINK_UP:
				ast_verbose("MTP2 link up (SLC %d)\n", e->gen.data);
				break;
			case MTP2_LINK_DOWN:
				ast_log(LOG_WARNING, "MTP2 link down (SLC %d)\n", e->gen.data);
				break;
			case ISUP_EVENT_CPG:
				chanpos = ss7_find_cic_gripe(linkset, e->cpg.cic, e->cpg.opc, "CPG");
				if (chanpos < 0) {
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
				ast_verbose("Resetting CIC %d\n", e->rsc.cic);
				chanpos = ss7_find_cic_gripe(linkset, e->rsc.cic, e->rsc.opc, "RSC");
				if (chanpos < 0) {
					break;
				}
				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				callid = func_ss7_linkset_callid(linkset, chanpos);
				sig_ss7_set_inservice(p, 1);
				sig_ss7_set_remotelyblocked(p, 0);
				isup_set_call_dpc(e->rsc.call, p->dpc);
				sig_ss7_lock_owner(linkset, chanpos);
				p->ss7call = NULL;
				if (p->owner) {
					ss7_queue_pvt_cause_data(p->owner, "SS7 ISUP_EVENT_RSC", AST_CAUSE_INTERWORKING);
					ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_DEV);
					ast_channel_unlock(p->owner);
				}
				sig_ss7_unlock_private(p);
				isup_rlc(ss7, e->rsc.call);
				break;
			case ISUP_EVENT_GRS:
				ast_debug(1, "Got Reset for CICs %d to %d: Acknowledging\n", e->grs.startcic, e->grs.endcic);
				chanpos = ss7_find_cic_gripe(linkset, e->grs.startcic, e->grs.opc, "GRS");
				if (chanpos < 0) {
					break;
				}
				p = linkset->pvts[chanpos];
				isup_gra(ss7, e->grs.startcic, e->grs.endcic, e->grs.opc);
				ss7_block_cics(linkset, e->grs.startcic, e->grs.endcic, e->grs.opc, NULL, 0);
				ss7_hangup_cics(linkset, e->grs.startcic, e->grs.endcic, e->grs.opc);
				break;
			case ISUP_EVENT_CQM:
				ast_debug(1, "Got Circuit group query message from CICs %d to %d\n", e->cqm.startcic, e->cqm.endcic);
				ss7_handle_cqm(linkset, e->cqm.startcic, e->cqm.endcic, e->cqm.opc);
				break;
			case ISUP_EVENT_GRA:
				ast_verbose("Got reset acknowledgement from CIC %d to %d.\n", e->gra.startcic, e->gra.endcic);
				ss7_inservice(linkset, e->gra.startcic, e->gra.endcic, e->gra.opc);
				ss7_block_cics(linkset, e->gra.startcic, e->gra.endcic, e->gra.opc, e->gra.status, 1);
				break;
			case ISUP_EVENT_IAM:
				ast_debug(1, "Got IAM for CIC %d and called number %s, calling number %s\n", e->iam.cic, e->iam.called_party_num, e->iam.calling_party_num);
				chanpos = ss7_find_cic_gripe(linkset, e->iam.cic, e->iam.opc, "IAM");
				if (chanpos < 0) {
					isup_rel(ss7, e->iam.call, -1);
					break;
				}
				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				sig_ss7_lock_owner(linkset, chanpos);
				if (p->call_level != SIG_SS7_CALL_LEVEL_IDLE) {
					/*
					 * Detected glare/dual-seizure
					 *
					 * Always abort both calls since we can't implement the dual
					 * seizure procedures due to our channel assignment architecture
					 * and the fact that we cannot tell libss7 to discard its call
					 * structure to ignore the incoming IAM.
					 */
					ast_debug(1,
						"Linkset %d: SS7 IAM glare on CIC/DPC %d/%d.  Dropping both calls.\n",
						linkset->span, e->iam.cic, e->iam.opc);
					if (p->call_level == SIG_SS7_CALL_LEVEL_ALLOCATED) {
						/*
						 * We have not sent our IAM yet and we never will at this point.
						 */
						p->alreadyhungup = 1;
						isup_rel(ss7, e->iam.call, AST_CAUSE_NORMAL_CIRCUIT_CONGESTION);
					}
					p->call_level = SIG_SS7_CALL_LEVEL_GLARE;
					if (p->owner) {
						ss7_queue_pvt_cause_data(p->owner, "SS7 ISUP_EVENT_IAM (glare)", AST_CAUSE_NORMAL_CIRCUIT_CONGESTION);
						ast_channel_hangupcause_set(p->owner, AST_CAUSE_NORMAL_CIRCUIT_CONGESTION);
						ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_DEV);
						ast_channel_unlock(p->owner);
					}
					sig_ss7_unlock_private(p);
					break;
				}
				/*
				 * The channel should not have an owner at this point since we
				 * are in the process of creating an owner for it.
				 */
				ast_assert(!p->owner);

				if (!sig_ss7_is_chan_available(p)) {
					/* Circuit is likely blocked or in alarm. */
					isup_rel(ss7, e->iam.call, AST_CAUSE_NORMAL_CIRCUIT_CONGESTION);
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
				} else
					p->cid_num[0] = 0;

				/* Set DNID */
				if (!ast_strlen_zero(e->iam.called_party_num)) {
					ss7_apply_plan_to_number(p->exten, sizeof(p->exten), linkset,
						e->iam.called_party_num, e->iam.called_nai);
				} else {
					p->exten[0] = '\0';
				}
				sig_ss7_set_dnid(p, p->exten);

				if (p->immediate) {
					p->exten[0] = 's';
					p->exten[1] = '\0';
				} else if (!ast_strlen_zero(e->iam.called_party_num)) {
					char *st;
					ss7_apply_plan_to_number(p->exten, sizeof(p->exten), linkset, e->iam.called_party_num, e->iam.called_nai);
					st = strchr(p->exten, '#');
					if (st) {
						*st = '\0';
					}
				} else {
					p->exten[0] = '\0';
				}

				p->cid_ani[0] = '\0';
				if ((p->use_callerid) && (!ast_strlen_zero(e->iam.generic_name)))
					ast_copy_string(p->cid_name, e->iam.generic_name, sizeof(p->cid_name));
				else
					p->cid_name[0] = '\0';

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
				ast_copy_string(p->orig_called_num, e->iam.orig_called_num, sizeof(p->orig_called_num));
				ast_copy_string(p->redirecting_num, e->iam.redirecting_num, sizeof(p->redirecting_num));
				ast_copy_string(p->generic_name, e->iam.generic_name, sizeof(p->generic_name));
				p->calling_party_cat = e->iam.calling_party_cat;

				sig_ss7_set_caller_id(p);

				if (ast_exists_extension(NULL, p->context, p->exten, 1, p->cid_num)) {
					if (e->iam.cot_check_required) {
						p->call_level = SIG_SS7_CALL_LEVEL_CONTINUITY;
						sig_ss7_loopback(p, 1);
					} else {
						ss7_start_call(p, linkset);
					}
				} else {
					ast_debug(1, "Call on CIC for unconfigured extension %s\n", p->exten);
					p->alreadyhungup = 1;
					isup_rel(ss7, e->iam.call, AST_CAUSE_UNALLOCATED);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_COT:
				chanpos = ss7_find_cic_gripe(linkset, e->cot.cic, e->cot.opc, "COT");
				if (chanpos < 0) {
					isup_rel(ss7, e->cot.call, -1);
					break;
				}
				p = linkset->pvts[chanpos];

				sig_ss7_lock_private(p);
				if (p->loopedback) {
					sig_ss7_loopback(p, 0);
					ss7_start_call(p, linkset);
				}
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_CCR:
				ast_debug(1, "Got CCR request on CIC %d\n", e->ccr.cic);
				chanpos = ss7_find_cic_gripe(linkset, e->ccr.cic, e->ccr.opc, "CCR");
				if (chanpos < 0) {
					break;
				}

				p = linkset->pvts[chanpos];

				sig_ss7_lock_private(p);
				sig_ss7_loopback(p, 1);
				sig_ss7_unlock_private(p);

				isup_lpa(linkset->ss7, e->ccr.cic, p->dpc);
				break;
			case ISUP_EVENT_CVT:
				ast_debug(1, "Got CVT request on CIC %d\n", e->cvt.cic);
				chanpos = ss7_find_cic_gripe(linkset, e->cvt.cic, e->cvt.opc, "CVT");
				if (chanpos < 0) {
					break;
				}

				p = linkset->pvts[chanpos];

				sig_ss7_lock_private(p);
				sig_ss7_loopback(p, 1);
				sig_ss7_unlock_private(p);

				isup_cvr(linkset->ss7, e->cvt.cic, p->dpc);
				break;
			case ISUP_EVENT_REL:
				chanpos = ss7_find_cic_gripe(linkset, e->rel.cic, e->rel.opc, "REL");
				if (chanpos < 0) {
					/* Continue hanging up the call anyway. */
					isup_rlc(ss7, e->rel.call);
					break;
				}
				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				callid = func_ss7_linkset_callid(linkset, chanpos);
				sig_ss7_lock_owner(linkset, chanpos);
				if (p->owner) {
					snprintf(cause_str, sizeof(cause_str), "SS7 ISUP_EVENT_REL (%d)", e->rel.cause);
					ss7_queue_pvt_cause_data(p->owner, cause_str, e->rel.cause);

					ast_channel_hangupcause_set(p->owner, e->rel.cause);
					ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_DEV);
					ast_channel_unlock(p->owner);
				}

				/* End the loopback if we have one */
				sig_ss7_loopback(p, 0);

				isup_rlc(ss7, e->rel.call);
				p->ss7call = NULL;

				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_ACM:
				chanpos = ss7_find_cic_gripe(linkset, e->acm.cic, e->acm.opc, "ACM");
				if (chanpos < 0) {
					isup_rel(ss7, e->acm.call, -1);
					break;
				}
				{
					p = linkset->pvts[chanpos];

					ast_debug(1, "Queueing frame from SS7_EVENT_ACM on CIC %d\n", p->cic);

					if (e->acm.call_ref_ident > 0) {
						p->rlt = 1; /* Setting it but not using it here*/
					}

					sig_ss7_lock_private(p);
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
					sig_ss7_unlock_private(p);
				}
				break;
			case ISUP_EVENT_CGB:
				chanpos = ss7_find_cic_gripe(linkset, e->cgb.startcic, e->cgb.opc, "CGB");
				if (chanpos < 0) {
					break;
				}
				p = linkset->pvts[chanpos];
				ss7_block_cics(linkset, e->cgb.startcic, e->cgb.endcic, e->cgb.opc, e->cgb.status, 1);
				isup_cgba(linkset->ss7, e->cgb.startcic, e->cgb.endcic, e->cgb.opc, e->cgb.status, e->cgb.type);
				break;
			case ISUP_EVENT_CGU:
				chanpos = ss7_find_cic_gripe(linkset, e->cgu.startcic, e->cgu.opc, "CGU");
				if (chanpos < 0) {
					break;
				}
				p = linkset->pvts[chanpos];
				ss7_block_cics(linkset, e->cgu.startcic, e->cgu.endcic, e->cgu.opc, e->cgu.status, 0);
				isup_cgua(linkset->ss7, e->cgu.startcic, e->cgu.endcic, e->cgu.opc, e->cgu.status, e->cgu.type);
				break;
			case ISUP_EVENT_UCIC:
				chanpos = ss7_find_cic_gripe(linkset, e->ucic.cic, e->ucic.opc, "UCIC");
				if (chanpos < 0) {
					break;
				}
				p = linkset->pvts[chanpos];
				ast_debug(1, "Unequiped Circuit Id Code on CIC %d\n", e->ucic.cic);
				sig_ss7_lock_private(p);
				sig_ss7_set_remotelyblocked(p, 1);
				sig_ss7_set_inservice(p, 0);
				sig_ss7_unlock_private(p);/* doesn't require a SS7 acknowledgement */
				break;
			case ISUP_EVENT_BLO:
				chanpos = ss7_find_cic_gripe(linkset, e->blo.cic, e->blo.opc, "BLO");
				if (chanpos < 0) {
					break;
				}
				p = linkset->pvts[chanpos];
				ast_debug(1, "Blocking CIC %d\n", e->blo.cic);
				sig_ss7_lock_private(p);
				sig_ss7_set_remotelyblocked(p, 1);
				sig_ss7_unlock_private(p);
				isup_bla(linkset->ss7, e->blo.cic, p->dpc);
				break;
			case ISUP_EVENT_BLA:
				chanpos = ss7_find_cic_gripe(linkset, e->bla.cic, e->bla.opc, "BLA");
				if (chanpos < 0) {
					break;
				}
				ast_debug(1, "Blocking CIC %d\n", e->bla.cic);
				p = linkset->pvts[chanpos];
				sig_ss7_lock_private(p);
				sig_ss7_set_locallyblocked(p, 1);
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_UBL:
				chanpos = ss7_find_cic_gripe(linkset, e->ubl.cic, e->ubl.opc, "UBL");
				if (chanpos < 0) {
					break;
				}
				p = linkset->pvts[chanpos];
				ast_debug(1, "Unblocking CIC %d\n", e->ubl.cic);
				sig_ss7_lock_private(p);
				sig_ss7_set_remotelyblocked(p, 0);
				sig_ss7_unlock_private(p);
				isup_uba(linkset->ss7, e->ubl.cic, p->dpc);
				break;
			case ISUP_EVENT_UBA:
				chanpos = ss7_find_cic_gripe(linkset, e->uba.cic, e->uba.opc, "UBA");
				if (chanpos < 0) {
					break;
				}
				p = linkset->pvts[chanpos];
				ast_debug(1, "Unblocking CIC %d\n", e->uba.cic);
				sig_ss7_lock_private(p);
				sig_ss7_set_locallyblocked(p, 0);
				sig_ss7_unlock_private(p);
				break;
			case ISUP_EVENT_CON:
			case ISUP_EVENT_ANM:
				if (e->e == ISUP_EVENT_CON) {
					chanpos = ss7_find_cic_gripe(linkset, e->con.cic, e->con.opc, "CON");
					if (chanpos < 0) {
						isup_rel(ss7, e->con.call, -1);
						break;
					}
				} else {
					chanpos = ss7_find_cic_gripe(linkset, e->anm.cic, e->anm.opc, "ANM");
					if (chanpos < 0) {
						isup_rel(ss7, e->anm.call, -1);
						break;
					}
				}

				{
					p = linkset->pvts[chanpos];
					sig_ss7_lock_private(p);
					callid = func_ss7_linkset_callid(linkset, chanpos);
					if (p->call_level < SIG_SS7_CALL_LEVEL_CONNECT) {
						p->call_level = SIG_SS7_CALL_LEVEL_CONNECT;
					}
					sig_ss7_queue_control(linkset, chanpos, AST_CONTROL_ANSWER);
					sig_ss7_set_dialing(p, 0);
					sig_ss7_open_media(p);
					sig_ss7_set_echocanceller(p, 1);
					sig_ss7_unlock_private(p);
				}
				break;
			case ISUP_EVENT_RLC:
				/* XXX Call ptr should be passed up from libss7! */
				chanpos = ss7_find_cic_gripe(linkset, e->rlc.cic, e->rlc.opc, "RLC");
				if (chanpos < 0) {
					break;
				}
				{
					p = linkset->pvts[chanpos];
					sig_ss7_lock_private(p);
					callid = func_ss7_linkset_callid(linkset, chanpos);
					if (p->alreadyhungup) {
						if (!p->owner) {
							p->call_level = SIG_SS7_CALL_LEVEL_IDLE;
						}
						p->ss7call = NULL;
					}
					sig_ss7_unlock_private(p);
				}
				break;
			case ISUP_EVENT_FAA:
				/*!
				 * \todo The handling of the SS7 FAA message is not good and I
				 * don't know enough to handle it correctly.
				 */
				chanpos = ss7_find_cic_gripe(linkset, e->faa.cic, e->faa.opc, "FAA");
				if (chanpos < 0) {
					isup_rel(linkset->ss7, e->faa.call, -1);
					break;
				}
				{
					/* XXX FAR and FAA used for something dealing with transfers? */
					p = linkset->pvts[chanpos];
					ast_debug(1, "FAA received on CIC %d\n", e->faa.cic);
					sig_ss7_lock_private(p);
					callid = func_ss7_linkset_callid(linkset, chanpos);
					if (p->alreadyhungup){
						if (!p->owner) {
							p->call_level = SIG_SS7_CALL_LEVEL_IDLE;
						}
						/* XXX We seem to be leaking the isup call structure here. */
						p->ss7call = NULL;
						ast_log(LOG_NOTICE, "Received FAA and we haven't sent FAR.  Ignoring.\n");
					}
					sig_ss7_unlock_private(p);
				}
				break;
			default:
				ast_debug(1, "Unknown event %s\n", ss7_event2str(e->e));
				break;
			}

			/* Call ID stuff needs to be cleaned up here */
			if (callid) {
				callid = ast_callid_unref(callid);
				ast_callid_threadassoc_remove();
			}
		}
		ast_mutex_unlock(&linkset->lock);
	}

	return 0;
}

static inline void ss7_rel(struct sig_ss7_linkset *ss7)
{
	ast_mutex_unlock(&ss7->lock);
}

static void ss7_grab(struct sig_ss7_chan *pvt, struct sig_ss7_linkset *ss7)
{
	/* Grab the lock first */
	while (ast_mutex_trylock(&ss7->lock)) {
		/* Avoid deadlock */
		sig_ss7_deadlock_avoidance_private(pvt);
	}
	/* Then break the poll */
	if (ss7->master != AST_PTHREADT_NULL) {
		pthread_kill(ss7->master, SIGURG);
	}
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
int sig_ss7_add_sigchan(struct sig_ss7_linkset *linkset, int which, int ss7type, int transport, int inalarm, int networkindicator, int pointcode, int adjpointcode)
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

	if (ss7_add_link(linkset->ss7, transport, linkset->fds[which])) {
		ast_log(LOG_WARNING, "Could not add SS7 link!\n");
	}

	if (inalarm) {
		linkset->linkstate[which] = LINKSTATE_DOWN | LINKSTATE_INALARM;
		ss7_link_alarm(linkset->ss7, linkset->fds[which]);
	} else {
		linkset->linkstate[which] = LINKSTATE_DOWN;
		ss7_link_noalarm(linkset->ss7, linkset->fds[which]);
	}

	ss7_set_adjpc(linkset->ss7, linkset->fds[which], adjpointcode);

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
		p->call_level = SIG_SS7_CALL_LEVEL_ALLOCATED;
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

	p->ss7call = isup_new_call(p->ss7->ss7);
	if (!p->ss7call) {
		ss7_rel(p->ss7);
		ast_log(LOG_ERROR, "Unable to allocate new SS7 call!\n");
		return -1;
	}

	called_nai_strip = 0;
	ss7_called_nai = p->ss7->called_nai;
	if (ss7_called_nai == SS7_NAI_DYNAMIC) { /* compute dynamically */
		if (strncmp(c + p->stripmsd, p->ss7->internationalprefix, strlen(p->ss7->internationalprefix)) == 0) {
			called_nai_strip = strlen(p->ss7->internationalprefix);
			ss7_called_nai = SS7_NAI_INTERNATIONAL;
		} else if (strncmp(c + p->stripmsd, p->ss7->nationalprefix, strlen(p->ss7->nationalprefix)) == 0) {
			called_nai_strip = strlen(p->ss7->nationalprefix);
			ss7_called_nai = SS7_NAI_NATIONAL;
		} else {
			ss7_called_nai = SS7_NAI_SUBSCRIBER;
		}
	}
	isup_set_called(p->ss7call, c + p->stripmsd + called_nai_strip, ss7_called_nai, p->ss7->ss7);

	calling_nai_strip = 0;
	ss7_calling_nai = p->ss7->calling_nai;
	if ((l != NULL) && (ss7_calling_nai == SS7_NAI_DYNAMIC)) { /* compute dynamically */
		if (strncmp(l, p->ss7->internationalprefix, strlen(p->ss7->internationalprefix)) == 0) {
			calling_nai_strip = strlen(p->ss7->internationalprefix);
			ss7_calling_nai = SS7_NAI_INTERNATIONAL;
		} else if (strncmp(l, p->ss7->nationalprefix, strlen(p->ss7->nationalprefix)) == 0) {
			calling_nai_strip = strlen(p->ss7->nationalprefix);
			ss7_calling_nai = SS7_NAI_NATIONAL;
		} else {
			ss7_calling_nai = SS7_NAI_SUBSCRIBER;
		}
	}
	isup_set_calling(p->ss7call, l ? (l + calling_nai_strip) : NULL, ss7_calling_nai,
		p->use_callingpres ? cid_pres2ss7pres(ast_channel_connected(ast)->id.number.presentation) : (l ? SS7_PRESENTATION_ALLOWED : SS7_PRESENTATION_RESTRICTED),
		p->use_callingpres ? cid_pres2ss7screen(ast_channel_connected(ast)->id.number.presentation) : SS7_SCREENING_USER_PROVIDED);

	isup_set_oli(p->ss7call, ast_channel_connected(ast)->ani2);
	isup_init_call(p->ss7->ss7, p->ss7call, p->cic, p->dpc);

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
	if ((send_far) && ((strncmp("NO", send_far, strlen(send_far))) != 0 ))
		(isup_far(p->ss7->ss7, p->ss7call));

	p->call_level = SIG_SS7_CALL_LEVEL_SETUP;
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
	int res = 0;

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
		if (!p->alreadyhungup) {
			const char *cause = pbx_builtin_getvar_helper(ast,"SS7_CAUSE");
			int icause = ast_channel_hangupcause(ast) ? ast_channel_hangupcause(ast) : -1;

			if (cause) {
				if (atoi(cause)) {
					icause = atoi(cause);
				}
			}
			isup_rel(p->ss7->ss7, p->ss7call, icause);
			p->alreadyhungup = 1;
		}
	}
	ss7_rel(p->ss7);

	return res;
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
		p->call_level = SIG_SS7_CALL_LEVEL_CONNECT;
	}
	sig_ss7_open_media(p);
	res = isup_anm(p->ss7->ss7, p->ss7call);
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
 * \brief SS7 answer channel.
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
			p->call_level = SIG_SS7_CALL_LEVEL_ALERTING;
			if ((isup_far(p->ss7->ss7, p->ss7call)) != -1) {
				p->rlt = 1;
			}

			/* No need to send CPG if call will be RELEASE */
			if (p->rlt != 1) {
				isup_cpg(p->ss7->ss7, p->ss7call, CPG_EVENT_ALERTING);
			}
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
			p->progress = 1;/* No need to send inband-information progress again. */
			isup_cpg(p->ss7->ss7, p->ss7call, CPG_EVENT_INBANDINFO);
			ss7_rel(p->ss7);

			/* enable echo canceler here on SS7 calls */
			sig_ss7_set_echocanceller(p, 1);
		} else {
			ss7_rel(p->ss7);
		}
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
struct ast_channel *sig_ss7_request(struct sig_ss7_chan *p, enum sig_ss7_law law, const struct ast_channel *requestor, int transfercapability)
{
	struct ast_channel *ast;

	/* Companding law is determined by SS7 signaling type. */
	if (p->ss7->type == SS7_ITU) {
		law = SIG_SS7_ALAW;
	} else {
		law = SIG_SS7_ULAW;
	}

	sig_ss7_set_outgoing(p, 1);
	ast = sig_ss7_new_ast_channel(p, AST_STATE_RESERVED, law, transfercapability, p->exten, requestor);
	if (!ast) {
		sig_ss7_set_outgoing(p, 0);

		/* Release the allocated channel.  Only have to deal with the linkset lock. */
		ast_mutex_lock(&p->ss7->lock);
		p->call_level = SIG_SS7_CALL_LEVEL_IDLE;
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
