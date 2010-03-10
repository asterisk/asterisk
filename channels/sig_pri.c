/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2009, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief PRI signaling module
 *
 * \author Matthew Fredrickson <creslin@digium.com>
 */


#include "asterisk.h"

#ifdef HAVE_PRI

#include <errno.h>
#include <ctype.h>
#include <signal.h>

#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/file.h"
#include "asterisk/callerid.h"
#include "asterisk/say.h"
#include "asterisk/manager.h"
#include "asterisk/astdb.h"
#include "asterisk/causes.h"
#include "asterisk/musiconhold.h"
#include "asterisk/cli.h"
#include "asterisk/transcap.h"
#include "asterisk/features.h"

#include "sig_pri.h"
#ifndef PRI_EVENT_FACILITY
#error please update libpri
#endif

/* define this to send PRI user-user information elements */
#undef SUPPORT_USERUSER

#if 0
#define DEFAULT_PRI_DEBUG (PRI_DEBUG_Q931_DUMP | PRI_DEBUG_Q921_DUMP | PRI_DEBUG_Q921_RAW | PRI_DEBUG_Q921_STATE)
#else
#define DEFAULT_PRI_DEBUG 0
#endif

static int pri_matchdigittimeout = 3000;

static int pri_gendigittimeout = 8000;

#define DCHAN_NOTINALARM  (1 << 0)
#define DCHAN_UP          (1 << 1)

/* Defines to help decode the encoded event channel id. */
#define PRI_CHANNEL(p)	((p) & 0xff)
#define PRI_SPAN(p)		(((p) >> 8) & 0xff)
#define PRI_EXPLICIT	(1 << 16)
#define PRI_CIS_CALL	(1 << 17)	/* Call is using the D channel only. */
#define PRI_HELD_CALL	(1 << 18)


#define DCHAN_AVAILABLE	(DCHAN_NOTINALARM | DCHAN_UP)

#define PRI_DEADLOCK_AVOIDANCE(p) \
	do { \
		sig_pri_unlock_private(p); \
		usleep(1); \
		sig_pri_lock_private(p); \
	} while (0)

static int pri_active_dchan_index(struct sig_pri_pri *pri);

static inline void pri_rel(struct sig_pri_pri *pri)
{
	ast_mutex_unlock(&pri->lock);
}

static unsigned int PVT_TO_CHANNEL(struct sig_pri_chan *p)
{
	int res = (((p)->prioffset) | ((p)->logicalspan << 8) | (p->mastertrunkgroup ? PRI_EXPLICIT : 0));
	ast_debug(5, "prioffset: %d mastertrunkgroup: %d logicalspan: %d result: %d\n",
		p->prioffset, p->mastertrunkgroup, p->logicalspan, res);

	return res;
}

static void sig_pri_handle_dchan_exception(struct sig_pri_pri *pri, int index)
{
	if (pri->calls->handle_dchan_exception)
		pri->calls->handle_dchan_exception(pri, index);
}

static void sig_pri_set_dialing(struct sig_pri_chan *p, int flag)
{
	if (p->calls->set_dialing)
		p->calls->set_dialing(p->chan_pvt, flag);
}

static void sig_pri_set_digital(struct sig_pri_chan *p, int flag)
{
	p->digital = flag;
	if (p->calls->set_digital)
		p->calls->set_digital(p->chan_pvt, flag);
}

/*!
 * \internal
 * \brief Set the caller id information in the parent module.
 * \since 1.8
 *
 * \param p sig_pri channel structure.
 *
 * \return Nothing
 */
static void sig_pri_set_caller_id(struct sig_pri_chan *p)
{
	struct ast_party_caller caller;

	if (p->calls->set_callerid) {
		ast_party_caller_init(&caller);
		caller.id.number = p->cid_num;
		caller.id.name = p->cid_name;
		if (!ast_strlen_zero(p->cid_subaddr)) {
			caller.id.subaddress.valid = 1;
			//caller.id.subaddress.type = 0;/* nsap */
			//caller.id.subaddress.odd_even_indicator = 0;
			caller.id.subaddress.str = p->cid_subaddr;
		}
		caller.id.number_type = p->cid_ton;
		caller.id.number_presentation = p->callingpres;
		caller.ani = p->cid_ani;
		caller.ani2 = p->cid_ani2;
		p->calls->set_callerid(p->chan_pvt, &caller);
	}
}

/*!
 * \internal
 * \brief Set the Dialed Number Identifier.
 * \since 1.8
 *
 * \param p sig_pri channel structure.
 * \param dnid Dialed Number Identifier string.
 *
 * \return Nothing
 */
static void sig_pri_set_dnid(struct sig_pri_chan *p, const char *dnid)
{
	if (p->calls->set_dnid) {
		p->calls->set_dnid(p->chan_pvt, dnid);
	}
}

/*!
 * \internal
 * \brief Set the Redirecting Directory Number Information Service (RDNIS).
 * \since 1.8
 *
 * \param p sig_pri channel structure.
 * \param rdnis Redirecting Directory Number Information Service (RDNIS) string.
 *
 * \return Nothing
 */
static void sig_pri_set_rdnis(struct sig_pri_chan *p, const char *rdnis)
{
	if (p->calls->set_rdnis) {
		p->calls->set_rdnis(p->chan_pvt, rdnis);
	}
}

static void sig_pri_unlock_private(struct sig_pri_chan *p)
{
	if (p->calls->unlock_private)
		p->calls->unlock_private(p->chan_pvt);
}

static void sig_pri_lock_private(struct sig_pri_chan *p)
{
	if (p->calls->lock_private)
		p->calls->lock_private(p->chan_pvt);
}

static inline int pri_grab(struct sig_pri_chan *p, struct sig_pri_pri *pri)
{
	int res;
	/* Grab the lock first */
	do {
		res = ast_mutex_trylock(&pri->lock);
		if (res) {
			PRI_DEADLOCK_AVOIDANCE(p);
		}
	} while (res);
	/* Then break the poll */
	pthread_kill(pri->master, SIGURG);
	return 0;
}

/*!
 * \internal
 * \brief Convert PRI redirecting reason to asterisk version.
 * \since 1.8
 *
 * \param pri_reason PRI redirecting reason.
 *
 * \return Equivalent asterisk redirecting reason value.
 */
static enum AST_REDIRECTING_REASON pri_to_ast_reason(int pri_reason)
{
	enum AST_REDIRECTING_REASON ast_reason;

	switch (pri_reason) {
	case PRI_REDIR_FORWARD_ON_BUSY:
		ast_reason = AST_REDIRECTING_REASON_USER_BUSY;
		break;
	case PRI_REDIR_FORWARD_ON_NO_REPLY:
		ast_reason = AST_REDIRECTING_REASON_NO_ANSWER;
		break;
	case PRI_REDIR_DEFLECTION:
		ast_reason = AST_REDIRECTING_REASON_DEFLECTION;
		break;
	case PRI_REDIR_UNCONDITIONAL:
		ast_reason = AST_REDIRECTING_REASON_UNCONDITIONAL;
		break;
	case PRI_REDIR_UNKNOWN:
	default:
		ast_reason = AST_REDIRECTING_REASON_UNKNOWN;
		break;
	}

	return ast_reason;
}

/*!
 * \internal
 * \brief Convert asterisk redirecting reason to PRI version.
 * \since 1.8
 *
 * \param ast_reason Asterisk redirecting reason.
 *
 * \return Equivalent PRI redirecting reason value.
 */
static int ast_to_pri_reason(enum AST_REDIRECTING_REASON ast_reason)
{
	int pri_reason;

	switch (ast_reason) {
	case AST_REDIRECTING_REASON_USER_BUSY:
		pri_reason = PRI_REDIR_FORWARD_ON_BUSY;
		break;
	case AST_REDIRECTING_REASON_NO_ANSWER:
		pri_reason = PRI_REDIR_FORWARD_ON_NO_REPLY;
		break;
	case AST_REDIRECTING_REASON_UNCONDITIONAL:
		pri_reason = PRI_REDIR_UNCONDITIONAL;
		break;
	case AST_REDIRECTING_REASON_DEFLECTION:
		pri_reason = PRI_REDIR_DEFLECTION;
		break;
	case AST_REDIRECTING_REASON_UNKNOWN:
	default:
		pri_reason = PRI_REDIR_UNKNOWN;
		break;
	}

	return pri_reason;
}

/*!
 * \internal
 * \brief Convert PRI number presentation to asterisk version.
 * \since 1.8
 *
 * \param pri_presentation PRI number presentation.
 *
 * \return Equivalent asterisk number presentation value.
 */
static int pri_to_ast_presentation(int pri_presentation)
{
	int ast_presentation;

	switch (pri_presentation) {
	case PRES_ALLOWED_USER_NUMBER_NOT_SCREENED:
		ast_presentation = AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
		break;
	case PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN:
		ast_presentation = AST_PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN;
		break;
	case PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN:
		ast_presentation = AST_PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN;
		break;
	case PRES_ALLOWED_NETWORK_NUMBER:
		ast_presentation = AST_PRES_ALLOWED_NETWORK_NUMBER;
		break;
	case PRES_PROHIB_USER_NUMBER_NOT_SCREENED:
		ast_presentation = AST_PRES_PROHIB_USER_NUMBER_NOT_SCREENED;
		break;
	case PRES_PROHIB_USER_NUMBER_PASSED_SCREEN:
		ast_presentation = AST_PRES_PROHIB_USER_NUMBER_PASSED_SCREEN;
		break;
	case PRES_PROHIB_USER_NUMBER_FAILED_SCREEN:
		ast_presentation = AST_PRES_PROHIB_USER_NUMBER_FAILED_SCREEN;
		break;
	case PRES_PROHIB_NETWORK_NUMBER:
		ast_presentation = AST_PRES_PROHIB_NETWORK_NUMBER;
		break;
	case PRES_NUMBER_NOT_AVAILABLE:
		ast_presentation = AST_PRES_NUMBER_NOT_AVAILABLE;
		break;
	default:
		ast_presentation = AST_PRES_PROHIB_USER_NUMBER_NOT_SCREENED;
		break;
	}

	return ast_presentation;
}

/*!
 * \internal
 * \brief Convert asterisk number presentation to PRI version.
 * \since 1.8
 *
 * \param ast_presentation Asterisk number presentation.
 *
 * \return Equivalent PRI number presentation value.
 */
static int ast_to_pri_presentation(int ast_presentation)
{
	int pri_presentation;

	switch (ast_presentation) {
	case AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED:
		pri_presentation = PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
		break;
	case AST_PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN:
		pri_presentation = PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN;
		break;
	case AST_PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN:
		pri_presentation = PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN;
		break;
	case AST_PRES_ALLOWED_NETWORK_NUMBER:
		pri_presentation = PRES_ALLOWED_NETWORK_NUMBER;
		break;
	case AST_PRES_PROHIB_USER_NUMBER_NOT_SCREENED:
		pri_presentation = PRES_PROHIB_USER_NUMBER_NOT_SCREENED;
		break;
	case AST_PRES_PROHIB_USER_NUMBER_PASSED_SCREEN:
		pri_presentation = PRES_PROHIB_USER_NUMBER_PASSED_SCREEN;
		break;
	case AST_PRES_PROHIB_USER_NUMBER_FAILED_SCREEN:
		pri_presentation = PRES_PROHIB_USER_NUMBER_FAILED_SCREEN;
		break;
	case AST_PRES_PROHIB_NETWORK_NUMBER:
		pri_presentation = PRES_PROHIB_NETWORK_NUMBER;
		break;
	case AST_PRES_NUMBER_NOT_AVAILABLE:
		pri_presentation = PRES_NUMBER_NOT_AVAILABLE;
		break;
	default:
		pri_presentation = PRES_PROHIB_USER_NUMBER_NOT_SCREENED;
		break;
	}

	return pri_presentation;
}

/*!
 * \internal
 * \brief Determine the overall presentation value for the given party.
 * \since 1.8
 *
 * \param id Party to determine the overall presentation value.
 *
 * \return Overall presentation value for the given party converted to ast values.
 */
static int overall_ast_presentation(const struct pri_party_id *id)
{
	int number_priority;
	int number_value;
	int number_screening;
	int name_priority;
	int name_value;

	/* Determine name presentation priority. */
	if (!id->name.valid) {
		name_value = PRI_PRES_UNAVAILABLE;
		name_priority = 3;
	} else {
		name_value = id->name.presentation & PRI_PRES_RESTRICTION;
		switch (name_value) {
		case PRI_PRES_RESTRICTED:
			name_priority = 0;
			break;
		case PRI_PRES_ALLOWED:
			name_priority = 1;
			break;
		case PRI_PRES_UNAVAILABLE:
			name_priority = 2;
			break;
		default:
			name_value = PRI_PRES_UNAVAILABLE;
			name_priority = 3;
			break;
		}
	}

	/* Determine number presentation priority. */
	if (!id->number.valid) {
		number_screening = PRI_PRES_USER_NUMBER_UNSCREENED;
		number_value = PRI_PRES_UNAVAILABLE;
		number_priority = 3;
	} else {
		number_screening = id->number.presentation & PRI_PRES_NUMBER_TYPE;
		number_value = id->number.presentation & PRI_PRES_RESTRICTION;
		switch (number_value) {
		case PRI_PRES_RESTRICTED:
			number_priority = 0;
			break;
		case PRI_PRES_ALLOWED:
			number_priority = 1;
			break;
		case PRI_PRES_UNAVAILABLE:
			number_priority = 2;
			break;
		default:
			number_screening = PRI_PRES_USER_NUMBER_UNSCREENED;
			number_value = PRI_PRES_UNAVAILABLE;
			number_priority = 3;
			break;
		}
	}

	/* Select the wining presentation value. */
	if (name_priority < number_priority) {
		number_value = name_value;
	}

	return pri_to_ast_presentation(number_value | number_screening);
}

#if defined(HAVE_PRI_SUBADDR)
/*!
 * \internal
 * \brief Fill in the asterisk party subaddress from the given PRI party subaddress.
 * \since 1.8
 *
 * \param ast_subaddress Asterisk party subaddress structure.
 * \param pri_subaddress PRI party subaddress structure.
 *
 * \return Nothing
 *
 */
static void sig_pri_set_subaddress(struct ast_party_subaddress *ast_subaddress, const struct pri_party_subaddress *pri_subaddress)
{
	char *cnum, *ptr;
	int x, len;

	if (ast_subaddress->str) {
		ast_free(ast_subaddress->str);
	}
	if (pri_subaddress->length <= 0) {
		ast_party_subaddress_init(ast_subaddress);
		return;
	}

	if (!pri_subaddress->type) {
		/* NSAP */
		ast_subaddress->str = ast_strdup((char *) pri_subaddress->data);
	} else {
		/* User Specified */
		if (!(cnum = ast_malloc(2 * pri_subaddress->length + 1))) {
			ast_party_subaddress_init(ast_subaddress);
			return;
		}

		ptr = cnum;
		len = pri_subaddress->length - 1; /* -1 account for zero based indexing */
		for (x = 0; x < len; ++x) {
			ptr += sprintf(ptr, "%02x", pri_subaddress->data[x]);
		}

		if (pri_subaddress->odd_even_indicator) {
			/* ODD */
			sprintf(ptr, "%01x", (pri_subaddress->data[len]) >> 4);
		} else {
			/* EVEN */
			sprintf(ptr, "%02x", pri_subaddress->data[len]);
		}
		ast_subaddress->str = cnum;
	}
	ast_subaddress->type = pri_subaddress->type;
	ast_subaddress->odd_even_indicator = pri_subaddress->odd_even_indicator;
	ast_subaddress->valid = 1;
}
#endif	/* defined(HAVE_PRI_SUBADDR) */

#if defined(HAVE_PRI_SUBADDR)
static unsigned char ast_pri_pack_hex_char(char c)
{
	unsigned char res;

	if (c < '0') {
		res = 0;
	} else if (c < ('9' + 1)) {
		res = c - '0';
	} else if (c < 'A') {
		res = 0;
	} else if (c < ('F' + 1)) {
		res = c - 'A' + 10;
	} else if (c < 'a') {
		res = 0;
	} else if (c < ('f' + 1)) {
		res = c - 'a' + 10;
	} else {
		res = 0;
	}
	return res;
}
#endif	/* defined(HAVE_PRI_SUBADDR) */

#if defined(HAVE_PRI_SUBADDR)
/*!
 * \internal
 * \brief Convert a null terminated hexadecimal string to a packed hex byte array.
 * \details left justified, with 0 padding if odd length.
 * \since 1.8
 *
 * \param dst pointer to packed byte array.
 * \param src pointer to null terminated hexadecimal string.
 * \param maxlen destination array size.
 *
 * \return Length of byte array
 *
 * \note The dst is not an ASCIIz string.
 * \note The src is an ASCIIz hex string.
 */
static int ast_pri_pack_hex_string(unsigned char *dst, char *src, int maxlen)
{
	int res = 0;
	int len = strlen(src);

	if (len > (2 * maxlen)) {
		len = 2 * maxlen;
	}

	res = len / 2 + len % 2;

	while (len > 1) {
		*dst = ast_pri_pack_hex_char(*src) << 4;
		src++;
		*dst |= ast_pri_pack_hex_char(*src);
		dst++, src++;
		len -= 2;
	}
	if (len) { /* 1 left */
		*dst = ast_pri_pack_hex_char(*src) << 4;
	}
	return res;
}
#endif	/* defined(HAVE_PRI_SUBADDR) */

#if defined(HAVE_PRI_SUBADDR)
/*!
 * \internal
 * \brief Fill in the PRI party subaddress from the given asterisk party subaddress.
 * \since 1.8
 *
 * \param pri_subaddress PRI party subaddress structure.
 * \param ast_subaddress Asterisk party subaddress structure.
 *
 * \return Nothing
 *
 * \note Assumes that pri_subaddress has been previously memset to zero.
 */
static void sig_pri_party_subaddress_from_ast(struct pri_party_subaddress *pri_subaddress, const struct ast_party_subaddress *ast_subaddress)
{
	if (ast_subaddress->valid && !ast_strlen_zero(ast_subaddress->str)) {
		pri_subaddress->type = ast_subaddress->type;
		if (!ast_subaddress->type) {
			/* 0 = NSAP */
			ast_copy_string((char *) pri_subaddress->data, ast_subaddress->str,
				sizeof(pri_subaddress->data));
			pri_subaddress->length = strlen((char *) pri_subaddress->data);
			pri_subaddress->odd_even_indicator = 0;
			pri_subaddress->valid = 1;
		} else {
			/* 2 = User Specified */
			/*
			 * Copy HexString to packed HexData,
			 * if odd length then right pad trailing byte with 0
			 */
			int length = ast_pri_pack_hex_string(pri_subaddress->data,
				ast_subaddress->str, sizeof(pri_subaddress->data));

			pri_subaddress->length = length;
			pri_subaddress->odd_even_indicator = (length & 1);
			pri_subaddress->valid = 1;
		}
	}
}
#endif	/* defined(HAVE_PRI_SUBADDR) */

/*!
 * \internal
 * \brief Fill in the PRI party id from the given asterisk party id.
 * \since 1.8
 *
 * \param pri_id PRI party id structure.
 * \param ast_id Asterisk party id structure.
 *
 * \return Nothing
 *
 * \note Assumes that pri_id has been previously memset to zero.
 */
static void sig_pri_party_id_from_ast(struct pri_party_id *pri_id, const struct ast_party_id *ast_id)
{
	int presentation;

	presentation = ast_to_pri_presentation(ast_id->number_presentation);
	if (!ast_strlen_zero(ast_id->name)) {
		pri_id->name.valid = 1;
		pri_id->name.presentation = presentation;
		pri_id->name.char_set = PRI_CHAR_SET_ISO8859_1;
		ast_copy_string(pri_id->name.str, ast_id->name, sizeof(pri_id->name.str));
	}
	if (!ast_strlen_zero(ast_id->number)) {
		pri_id->number.valid = 1;
		pri_id->number.presentation = presentation;
		pri_id->number.plan = ast_id->number_type;
		ast_copy_string(pri_id->number.str, ast_id->number, sizeof(pri_id->number.str));
	}
#if defined(HAVE_PRI_SUBADDR)
	sig_pri_party_subaddress_from_ast(&pri_id->subaddress, &ast_id->subaddress);
#endif	/* defined(HAVE_PRI_SUBADDR) */
}

/*!
 * \internal
 * \brief Update the PRI redirecting information for the current call.
 * \since 1.8
 *
 * \param pvt sig_pri private channel structure.
 * \param ast Asterisk channel
 *
 * \return Nothing
 *
 * \note Assumes that the PRI lock is already obtained.
 */
static void sig_pri_redirecting_update(struct sig_pri_chan *pvt, struct ast_channel *ast)
{
	struct pri_party_redirecting pri_redirecting;
	struct ast_party_redirecting ast_redirecting;

	/* Gather asterisk redirecting data */
	ast_redirecting = ast->redirecting;
	ast_redirecting.from.number = ast->cid.cid_rdnis;

/*! \todo XXX Original called data can be put in a channel data store that is inherited. */

	memset(&pri_redirecting, 0, sizeof(pri_redirecting));
	sig_pri_party_id_from_ast(&pri_redirecting.from, &ast_redirecting.from);
	sig_pri_party_id_from_ast(&pri_redirecting.to, &ast_redirecting.to);
	pri_redirecting.count = ast_redirecting.count;
	pri_redirecting.reason = ast_to_pri_reason(ast_redirecting.reason);

	pri_redirecting_update(pvt->pri->pri, pvt->call, &pri_redirecting);
}

/*!
 * \internal
 * \brief Reset DTMF detector.
 * \since 1.8
 *
 * \param p sig_pri channel structure.
 *
 * \return Nothing
 */
static void sig_pri_dsp_reset_and_flush_digits(struct sig_pri_chan *p)
{
	if (p->calls->dsp_reset_and_flush_digits) {
		p->calls->dsp_reset_and_flush_digits(p->chan_pvt);
	}
}

static int sig_pri_set_echocanceller(struct sig_pri_chan *p, int enable)
{
	if (p->calls->set_echocanceller)
		return p->calls->set_echocanceller(p->chan_pvt, enable);
	else
		return -1;
}

static void sig_pri_fixup_chans(struct sig_pri_chan *old_chan, struct sig_pri_chan *new_chan)
{
	if (old_chan->calls->fixup_chans)
		old_chan->calls->fixup_chans(old_chan->chan_pvt, new_chan->chan_pvt);
}

static int sig_pri_play_tone(struct sig_pri_chan *p, enum sig_pri_tone tone)
{
	if (p->calls->play_tone)
		return p->calls->play_tone(p->chan_pvt, tone);
	else
		return -1;
}

static struct ast_channel *sig_pri_new_ast_channel(struct sig_pri_chan *p, int state, int startpbx, int ulaw, int transfercapability, char *exten, const struct ast_channel *requestor)
{
	struct ast_channel *c;

	if (p->calls->new_ast_channel)
		c = p->calls->new_ast_channel(p->chan_pvt, state, startpbx, ulaw, transfercapability, exten, requestor);
	else
		return NULL;

	if (!p->owner)
		p->owner = c;
	p->isidlecall = 0;
	p->alreadyhungup = 0;
	if (transfercapability & AST_TRANS_CAP_DIGITAL) {
		c->transfercapability = transfercapability;
		pbx_builtin_setvar_helper(c, "TRANSFERCAPABILITY", ast_transfercapability2str(transfercapability));
		sig_pri_set_digital(p, 1);
	}

	return c;
}

struct ast_channel *sig_pri_request(struct sig_pri_chan *p, enum sig_pri_law law, const struct ast_channel *requestor, int transfercapability)
{
	struct ast_channel *ast;

	ast_log(LOG_DEBUG, "%s %d\n", __FUNCTION__, p->channel);

	p->outgoing = 1;
	ast = sig_pri_new_ast_channel(p, AST_STATE_RESERVED, 0, law, transfercapability, p->exten, requestor);
	if (!ast) {
		p->outgoing = 0;
	}
	return ast;
}

int pri_is_up(struct sig_pri_pri *pri)
{
	int x;
	for (x = 0; x < NUM_DCHANS; x++) {
		if (pri->dchanavail[x] == DCHAN_AVAILABLE)
			return 1;
	}
	return 0;
}

static char *pri_order(int level)
{
	switch (level) {
	case 0:
		return "Primary";
	case 1:
		return "Secondary";
	case 2:
		return "Tertiary";
	case 3:
		return "Quaternary";
	default:
		return "<Unknown>";
	}
}

/* Returns index of the active dchan */
static int pri_active_dchan_index(struct sig_pri_pri *pri)
{
	int x;

	for (x = 0; x < NUM_DCHANS; x++) {
		if ((pri->dchans[x] == pri->pri))
			return x;
	}

	ast_log(LOG_WARNING, "No active dchan found!\n");
	return -1;
}

static int pri_find_dchan(struct sig_pri_pri *pri)
{
	int oldslot = -1;
	struct pri *old;
	int newslot = -1;
	int x;
	old = pri->pri;
	for (x = 0; x < NUM_DCHANS; x++) {
		if ((pri->dchanavail[x] == DCHAN_AVAILABLE) && (newslot < 0))
			newslot = x;
		if (pri->dchans[x] == old) {
			oldslot = x;
		}
	}
	if (newslot < 0) {
		newslot = 0;
		/* This is annoying to see on non persistent layer 2 connections.  Let's not complain in that case */
		if (pri->sig != SIG_BRI_PTMP) {
			ast_log(LOG_WARNING, "No D-channels available!  Using Primary channel as D-channel anyway!\n");
		}
	}
	if (old && (oldslot != newslot))
		ast_log(LOG_NOTICE, "Switching from d-channel fd %d to fd %d!\n",
			pri->fds[oldslot], pri->fds[newslot]);
	pri->pri = pri->dchans[newslot];
	return 0;
}

static void pri_queue_frame(struct sig_pri_chan *p, struct ast_frame *f, struct sig_pri_pri *pri)
{
	/* We must unlock the PRI to avoid the possibility of a deadlock */
	if (pri)
		ast_mutex_unlock(&pri->lock);
	for (;;) {
		if (p->owner) {
			if (ast_channel_trylock(p->owner)) {
				PRI_DEADLOCK_AVOIDANCE(p);
			} else {
				ast_queue_frame(p->owner, f);
				ast_channel_unlock(p->owner);
				break;
			}
		} else
			break;
	}
	if (pri)
		ast_mutex_lock(&pri->lock);
}

static void pri_queue_control(struct sig_pri_chan *p, int subclass, struct sig_pri_pri *pri)
{
	struct ast_frame f = {AST_FRAME_CONTROL, };

	if (p->calls->queue_control) {
		p->calls->queue_control(p->chan_pvt, subclass);
	}

	f.subclass.integer = subclass;
	pri_queue_frame(p, &f, pri);
}

static int pri_find_principle(struct sig_pri_pri *pri, int channel, q931_call *call)
{
	int x;
	int span;
	int principle;

	if (channel < 0) {
		/* Channel is not picked yet. */
		return -1;
	}

	if (channel & PRI_HELD_CALL) {
		if (!call) {
			/* Cannot find a held call without a call. */
			return -1;
		}
		principle = -1;
		for (x = 0; x < pri->numchans; ++x) {
			if (pri->pvts[x]
				&& pri->pvts[x]->call == call) {
				principle = x;
				break;
			}
		}
		return principle;
	}

	span = PRI_SPAN(channel);
	if (!(channel & PRI_EXPLICIT)) {
		int index;

		index = pri_active_dchan_index(pri);
		if (index == -1) {
			return -1;
		}
		span = pri->dchan_logical_span[index];
	}

	channel = PRI_CHANNEL(channel);
	principle = -1;
	for (x = 0; x < pri->numchans; x++) {
		if (pri->pvts[x]
			&& pri->pvts[x]->prioffset == channel
			&& pri->pvts[x]->logicalspan == span
			&& !pri->pvts[x]->no_b_channel) {
			principle = x;
			break;
		}
	}

	return principle;
}

static int pri_fixup_principle(struct sig_pri_pri *pri, int principle, q931_call *call)
{
	int x;

	if (principle < 0 || pri->numchans <= principle) {
		/* Out of rannge */
		return -1;
	}
	if (!call) {
		/* No call */
		return principle;
	}
	if (pri->pvts[principle] && pri->pvts[principle]->call == call) {
		/* Call is already on the specified principle. */
		return principle;
	}

	/* Find the old principle location. */
	for (x = 0; x < pri->numchans; x++) {
		struct sig_pri_chan *new_chan;
		struct sig_pri_chan *old_chan;

		if (!pri->pvts[x] || pri->pvts[x]->call != call) {
			continue;
		}

		/* Found our call */
		new_chan = pri->pvts[principle];
		old_chan = pri->pvts[x];

		ast_verb(3, "Moving call from channel %d to channel %d\n",
			old_chan->channel, new_chan->channel);
		if (new_chan->owner) {
			ast_log(LOG_WARNING,
				"Can't fix up channel from %d to %d because %d is already in use\n",
				old_chan->channel, new_chan->channel, new_chan->channel);
			return -1;
		}

		sig_pri_fixup_chans(old_chan, new_chan);

		/* Fix it all up now */
		new_chan->owner = old_chan->owner;
		old_chan->owner = NULL;

		new_chan->call = old_chan->call;
		old_chan->call = NULL;

		/* Transfer flags from the old channel. */
		new_chan->alerting = old_chan->alerting;
		new_chan->alreadyhungup = old_chan->alreadyhungup;
		new_chan->isidlecall = old_chan->isidlecall;
		new_chan->proceeding = old_chan->proceeding;
		new_chan->progress = old_chan->progress;
		new_chan->setup_ack = old_chan->setup_ack;
		new_chan->outgoing = old_chan->outgoing;
		new_chan->digital = old_chan->digital;
		old_chan->alerting = 0;
		old_chan->alreadyhungup = 0;
		old_chan->isidlecall = 0;
		old_chan->proceeding = 0;
		old_chan->progress = 0;
		old_chan->setup_ack = 0;
		old_chan->outgoing = 0;
		old_chan->digital = 0;

		/* More stuff to transfer to the new channel. */
#if defined(HAVE_PRI_REVERSE_CHARGE)
		new_chan->reverse_charging_indication = old_chan->reverse_charging_indication;
#endif	/* defined(HAVE_PRI_REVERSE_CHARGE) */
#if defined(HAVE_PRI_SETUP_KEYPAD)
		strcpy(new_chan->keypad_digits, old_chan->keypad_digits);
#endif	/* defined(HAVE_PRI_SETUP_KEYPAD) */

		if (new_chan->no_b_channel) {
			/* Copy the real channel configuration to the no B channel interface. */
			new_chan->hidecallerid = old_chan->hidecallerid;
			new_chan->hidecalleridname = old_chan->hidecalleridname;
			new_chan->immediate = old_chan->immediate;
			new_chan->priexclusive = old_chan->priexclusive;
			new_chan->priindication_oob = old_chan->priindication_oob;
			new_chan->use_callerid = old_chan->use_callerid;
			new_chan->use_callingpres = old_chan->use_callingpres;
			new_chan->stripmsd = old_chan->stripmsd;
			strcpy(new_chan->context, old_chan->context);
			strcpy(new_chan->mohinterpret, old_chan->mohinterpret);

			/* Become a member of the old channel span/trunk-group. */
			new_chan->logicalspan = old_chan->logicalspan;
			new_chan->mastertrunkgroup = old_chan->mastertrunkgroup;
		}

		return principle;
	}
	ast_log(LOG_WARNING, "Call specified, but not found?\n");
	return -1;
}

static char * redirectingreason2str(int redirectingreason)
{
	switch (redirectingreason) {
	case 0:
		return "UNKNOWN";
	case 1:
		return "BUSY";
	case 2:
		return "NO_REPLY";
	case 0xF:
		return "UNCONDITIONAL";
	default:
		return "NOREDIRECT";
	}
}

static char *dialplan2str(int dialplan)
{
	if (dialplan == -1) {
		return("Dynamically set dialplan in ISDN");
	}
	return (pri_plan2str(dialplan));
}

static void apply_plan_to_number(char *buf, size_t size, const struct sig_pri_pri *pri, const char *number, const int plan)
{
	switch (plan) {
	case PRI_INTERNATIONAL_ISDN:		/* Q.931 dialplan == 0x11 international dialplan => prepend international prefix digits */
		snprintf(buf, size, "%s%s", pri->internationalprefix, number);
		break;
	case PRI_NATIONAL_ISDN:			/* Q.931 dialplan == 0x21 national dialplan => prepend national prefix digits */
		snprintf(buf, size, "%s%s", pri->nationalprefix, number);
		break;
	case PRI_LOCAL_ISDN:			/* Q.931 dialplan == 0x41 local dialplan => prepend local prefix digits */
		snprintf(buf, size, "%s%s", pri->localprefix, number);
		break;
	case PRI_PRIVATE:			/* Q.931 dialplan == 0x49 private dialplan => prepend private prefix digits */
		snprintf(buf, size, "%s%s", pri->privateprefix, number);
		break;
	case PRI_UNKNOWN:			/* Q.931 dialplan == 0x00 unknown dialplan => prepend unknown prefix digits */
		snprintf(buf, size, "%s%s", pri->unknownprefix, number);
		break;
	default:				/* other Q.931 dialplan => don't twiddle with callingnum */
		snprintf(buf, size, "%s", number);
		break;
	}
}

/*! \note Assumes the pri->lock is already obtained. */
static int pri_check_restart(struct sig_pri_pri *pri)
{
#if defined(HAVE_PRI_SERVICE_MESSAGES)
tryanotherpos:
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
	do {
		pri->resetpos++;
	} while (pri->resetpos < pri->numchans
		&& (!pri->pvts[pri->resetpos]
			|| pri->pvts[pri->resetpos]->no_b_channel
			|| pri->pvts[pri->resetpos]->call
			|| pri->pvts[pri->resetpos]->resetting));
	if (pri->resetpos < pri->numchans) {
#if defined(HAVE_PRI_SERVICE_MESSAGES)
		unsigned why;

		why = pri->pvts[pri->resetpos]->service_status;
		if (why) {
			ast_log(LOG_NOTICE, "span '%d' channel '%d' out-of-service (reason: %s), not sending RESTART\n", pri->span,
				pri->pvts[pri->resetpos]->channel, (why & SRVST_FAREND) ? (why & SRVST_NEAREND) ? "both ends" : "far end" : "near end");
			goto tryanotherpos;
		}
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */

		/* Mark the channel as resetting and restart it */
		pri->pvts[pri->resetpos]->resetting = 1;
		pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[pri->resetpos]));
	} else {
		pri->resetting = 0;
		time(&pri->lastreset);
	}
	return 0;
}

static int pri_find_empty_chan(struct sig_pri_pri *pri, int backwards)
{
	int x;
	if (backwards)
		x = pri->numchans;
	else
		x = 0;
	for (;;) {
		if (backwards && (x < 0))
			break;
		if (!backwards && (x >= pri->numchans))
			break;
		if (pri->pvts[x]
			&& !pri->pvts[x]->no_b_channel
			&& !pri->pvts[x]->inalarm
			&& !pri->pvts[x]->owner) {
			ast_debug(1, "Found empty available channel %d/%d\n",
				pri->pvts[x]->logicalspan, pri->pvts[x]->prioffset);
			return x;
		}
		if (backwards)
			x--;
		else
			x++;
	}
	return -1;
}

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief Find or create an empty no-B-channel interface to use.
 * \since 1.8
 *
 * \param pri sig_pri span controller to find interface.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval array-index into private pointer array on success.
 * \retval -1 on error.
 */
static int pri_find_empty_nobch(struct sig_pri_pri *pri)
{
	int idx;

	for (idx = 0; idx < pri->numchans; ++idx) {
		if (pri->pvts[idx]
			&& pri->pvts[idx]->no_b_channel
			&& !pri->pvts[idx]->inalarm
			&& !pri->pvts[idx]->owner) {
			ast_debug(1, "Found empty available no B channel interface\n");
			return idx;
		}
	}

	/* Need to create a new interface. */
	if (pri->calls->new_nobch_intf) {
		idx = pri->calls->new_nobch_intf(pri);
	} else {
		idx = -1;
	}
	return idx;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief Find the channel associated with the libpri call.
 * \since 1.8
 *
 * \param pri sig_pri span controller to find interface.
 * \param call LibPRI opaque call pointer to find.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval array-index into private pointer array on success.
 * \retval -1 on error.
 */
static int pri_find_pri_call(struct sig_pri_pri *pri, q931_call *call)
{
	int idx;

	for (idx = 0; idx < pri->numchans; ++idx) {
		if (pri->pvts[idx] && pri->pvts[idx]->call == call) {
			/* Found the channel */
			return idx;
		}
	}
	return -1;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

static void *do_idle_thread(void *v_pvt)
{
	struct sig_pri_chan *pvt = v_pvt;
	struct ast_channel *chan = pvt->owner;
	struct ast_frame *f;
	char ex[80];
	/* Wait up to 30 seconds for an answer */
	int newms, ms = 30000;

	ast_verb(3, "Initiating idle call on channel %s\n", chan->name);
	snprintf(ex, sizeof(ex), "%d/%s", pvt->channel, pvt->pri->idledial);
	if (ast_call(chan, ex, 0)) {
		ast_log(LOG_WARNING, "Idle dial failed on '%s' to '%s'\n", chan->name, ex);
		ast_hangup(chan);
		return NULL;
	}
	while ((newms = ast_waitfor(chan, ms)) > 0) {
		f = ast_read(chan);
		if (!f) {
			/* Got hangup */
			break;
		}
		if (f->frametype == AST_FRAME_CONTROL) {
			switch (f->subclass.integer) {
			case AST_CONTROL_ANSWER:
				/* Launch the PBX */
				ast_copy_string(chan->exten, pvt->pri->idleext, sizeof(chan->exten));
				ast_copy_string(chan->context, pvt->pri->idlecontext, sizeof(chan->context));
				chan->priority = 1;
				ast_verb(4, "Idle channel '%s' answered, sending to %s@%s\n", chan->name, chan->exten, chan->context);
				ast_pbx_run(chan);
				/* It's already hungup, return immediately */
				return NULL;
			case AST_CONTROL_BUSY:
				ast_verb(4, "Idle channel '%s' busy, waiting...\n", chan->name);
				break;
			case AST_CONTROL_CONGESTION:
				ast_verb(4, "Idle channel '%s' congested, waiting...\n", chan->name);
				break;
			};
		}
		ast_frfree(f);
		ms = newms;
	}
	/* Hangup the channel since nothing happend */
	ast_hangup(chan);
	return NULL;
}

static void *pri_ss_thread(void *data)
{
	struct sig_pri_chan *p = data;
	struct ast_channel *chan = p->owner;
	char exten[AST_MAX_EXTENSION];
	int res;
	int len;
	int timeout;

	if (!chan) {
		/* We lost the owner before we could get started. */
		return NULL;
	}

	/*
	 * In the bizarre case where the channel has become a zombie before we
	 * even get started here, abort safely.
	 */
	if (!chan->tech_pvt) {
		ast_log(LOG_WARNING, "Channel became a zombie before simple switch could be started (%s)\n", chan->name);
		ast_hangup(chan);
		return NULL;
	}

	ast_verb(3, "Starting simple switch on '%s'\n", chan->name);

	sig_pri_dsp_reset_and_flush_digits(p);

	/* Now loop looking for an extension */
	ast_copy_string(exten, p->exten, sizeof(exten));
	len = strlen(exten);
	res = 0;
	while ((len < AST_MAX_EXTENSION-1) && ast_matchmore_extension(chan, chan->context, exten, 1, p->cid_num)) {
		if (len && !ast_ignore_pattern(chan->context, exten))
			sig_pri_play_tone(p, -1);
		else
			sig_pri_play_tone(p, SIG_PRI_TONE_DIALTONE);
		if (ast_exists_extension(chan, chan->context, exten, 1, p->cid_num))
			timeout = pri_matchdigittimeout;
		else
			timeout = pri_gendigittimeout;
		res = ast_waitfordigit(chan, timeout);
		if (res < 0) {
			ast_log(LOG_DEBUG, "waitfordigit returned < 0...\n");
			ast_hangup(chan);
			return NULL;
		} else if (res) {
			exten[len++] = res;
			exten[len] = '\0';
		} else
			goto exit;
	}
	/* if no extension was received ('unspecified') on overlap call, use the 's' extension */
	if (ast_strlen_zero(exten)) {
		ast_verb(3, "Going to extension s|1 because of empty extension received on overlap call\n");
		exten[0] = 's';
		exten[1] = '\0';
	} else {
		if (chan->cid.cid_dnid) {
			ast_free(chan->cid.cid_dnid);
		}
		chan->cid.cid_dnid = ast_strdup(exten);
	}
	sig_pri_play_tone(p, -1);
	if (ast_exists_extension(chan, chan->context, exten, 1, p->cid_num)) {
		/* Start the real PBX */
		ast_copy_string(chan->exten, exten, sizeof(chan->exten));
		sig_pri_dsp_reset_and_flush_digits(p);
		if (p->pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING) {
			if (p->pri->pri) {		
				if (!pri_grab(p, p->pri)) {
					pri_proceeding(p->pri->pri, p->call, PVT_TO_CHANNEL(p), 0);
					p->proceeding = 1;
					pri_rel(p->pri);
				} else {
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
				}
			}
		}

		sig_pri_set_echocanceller(p, 1);
		ast_setstate(chan, AST_STATE_RING);
		res = ast_pbx_run(chan);
		if (res) {
			ast_log(LOG_WARNING, "PBX exited non-zero!\n");
		}
	} else {
		ast_log(LOG_DEBUG, "No such possible extension '%s' in context '%s'\n", exten, chan->context);
		chan->hangupcause = AST_CAUSE_UNALLOCATED;
		ast_hangup(chan);
		p->exten[0] = '\0';
		/* Since we send release complete here, we won't get one */
		p->call = NULL;
	}
	return NULL;

exit:
	res = sig_pri_play_tone(p, SIG_PRI_TONE_CONGESTION);
	if (res < 0)
		ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	ast_hangup(chan);
	return NULL;
}

void pri_event_alarm(struct sig_pri_pri *pri, int index, int before_start_pri)
{
	pri->dchanavail[index] &= ~(DCHAN_NOTINALARM | DCHAN_UP);
	if (!before_start_pri)
		pri_find_dchan(pri);
}

void pri_event_noalarm(struct sig_pri_pri *pri, int index, int before_start_pri)
{
	pri->dchanavail[index] |= DCHAN_NOTINALARM;
	if (!before_start_pri)
		pri_restart(pri->dchans[index]);
}

/*!
 * \internal
 * \brief Convert libpri party id into asterisk party id.
 * \since 1.8
 *
 * \param ast_id Asterisk party id structure to fill.  Must already be set initialized.
 * \param pri_id libpri party id structure containing source information.
 * \param pri Span controlling structure.
 *
 * \note The filled in ast_id structure needs to be destroyed by
 * ast_party_id_free() when it is no longer needed.
 *
 * \return Nothing
 */
static void sig_pri_party_id_convert(struct ast_party_id *ast_id,
	const struct pri_party_id *pri_id, struct sig_pri_pri *pri)
{
	char number[AST_MAX_EXTENSION];

	if (pri_id->name.valid) {
		ast_id->name = ast_strdup(pri_id->name.str);
	}
	if (pri_id->number.valid) {
		apply_plan_to_number(number, sizeof(number), pri, pri_id->number.str,
			pri_id->number.plan);
		ast_id->number = ast_strdup(number);
		ast_id->number_type = pri_id->number.plan;
	}
	if (pri_id->name.valid || pri_id->number.valid) {
		ast_id->number_presentation = overall_ast_presentation(pri_id);
	}
#if defined(HAVE_PRI_SUBADDR)
	if (pri_id->subaddress.valid) {
		sig_pri_set_subaddress(&ast_id->subaddress, &pri_id->subaddress);
	}
#endif	/* defined(HAVE_PRI_SUBADDR) */
}

/*!
 * \internal
 * \brief Convert libpri redirecting information into asterisk redirecting information.
 * \since 1.8
 *
 * \param ast_redirecting Asterisk redirecting structure to fill.
 * \param pri_redirecting libpri redirecting structure containing source information.
 * \param ast_guide Asterisk redirecting structure to use as an initialization guide.
 * \param pri Span controlling structure.
 *
 * \note The filled in ast_redirecting structure needs to be destroyed by
 * ast_party_redirecting_free() when it is no longer needed.
 *
 * \return Nothing
 */
static void sig_pri_redirecting_convert(struct ast_party_redirecting *ast_redirecting,
	const struct pri_party_redirecting *pri_redirecting,
	const struct ast_party_redirecting *ast_guide,
	struct sig_pri_pri *pri)
{
	ast_party_redirecting_set_init(ast_redirecting, ast_guide);

	sig_pri_party_id_convert(&ast_redirecting->from, &pri_redirecting->from, pri);
	sig_pri_party_id_convert(&ast_redirecting->to, &pri_redirecting->to, pri);
	ast_redirecting->count = pri_redirecting->count;
	ast_redirecting->reason = pri_to_ast_reason(pri_redirecting->reason);
}

/*!
 * \internal
 * \brief Determine if the given extension matches one of the MSNs in the pattern list.
 * \since 1.8
 *
 * \param msn_patterns Comma separated list of MSN patterns to match.
 * \param exten Extension to match in the MSN list.
 *
 * \retval 1 if matches.
 * \retval 0 if no match.
 */
static int sig_pri_msn_match(const char *msn_patterns, const char *exten)
{
	char *pattern;
	char *msn_list;
	char *list_tail;

	msn_list = strdupa(msn_patterns);

	list_tail = NULL;
	pattern = strtok_r(msn_list, ",", &list_tail);
	while (pattern) {
		pattern = ast_strip(pattern);
		if (!ast_strlen_zero(pattern) && ast_extension_match(pattern, exten)) {
			/* Extension matched the pattern. */
			return 1;
		}
		pattern = strtok_r(NULL, ",", &list_tail);
	}
	/* Did not match any pattern in the list. */
	return 0;
}

/*!
 * \internal
 * \brief Obtain the sig_pri owner channel lock if the owner exists.
 * \since 1.8
 *
 * \param pri sig_pri PRI control structure.
 * \param chanpos Channel position in the span.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_lock_owner(struct sig_pri_pri *pri, int chanpos)
{
	for (;;) {
		if (!pri->pvts[chanpos]->owner) {
			/* There is no owner lock to get. */
			break;
		}
		if (!ast_channel_trylock(pri->pvts[chanpos]->owner)) {
			/* We got the lock */
			break;
		}
		/* We must unlock the PRI to avoid the possibility of a deadlock */
		ast_mutex_unlock(&pri->lock);
		PRI_DEADLOCK_AVOIDANCE(pri->pvts[chanpos]);
		ast_mutex_lock(&pri->lock);
	}
}

/*!
 * \internal
 * \brief Handle the call associated PRI subcommand events.
 * \since 1.8
 *
 * \param pri sig_pri PRI control structure.
 * \param chanpos Channel position in the span.
 * \param event_id PRI event id
 * \param channel PRI encoded span/channel
 * \param subcmds Subcommands to process if any. (Could be NULL).
 * \param call_rsp libpri opaque call structure to send any responses toward.
 * Could be NULL either because it is not available or the call is for the
 * dummy call reference.  However, this should not be NULL in the cases that
 * need to use the pointer to send a response message back.
 *
 * \note Assumes the pri->lock is already obtained.
 * \note Assumes the sig_pri_lock_private(pri->pvts[chanpos]) is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_handle_subcmds(struct sig_pri_pri *pri, int chanpos, int event_id,
	int channel, const struct pri_subcommands *subcmds, q931_call *call_rsp)
{
	int index;
	struct ast_channel *owner;
	struct ast_party_redirecting ast_redirecting;

	if (!subcmds) {
		return;
	}
	for (index = 0; index < subcmds->counter_subcmd; ++index) {
		const struct pri_subcommand *subcmd = &subcmds->subcmd[index];

		switch (subcmd->cmd) {
		case PRI_SUBCMD_CONNECTED_LINE:
			sig_pri_lock_owner(pri, chanpos);
			owner = pri->pvts[chanpos]->owner;
			if (owner) {
				struct ast_party_connected_line ast_connected;
				int caller_id_update;

				/* Extract the connected line information */
				ast_party_connected_line_init(&ast_connected);
				sig_pri_party_id_convert(&ast_connected.id, &subcmd->u.connected_line.id,
					pri);

				caller_id_update = 0;
				if (ast_connected.id.name) {
					/* Save name for Caller-ID update */
					ast_copy_string(pri->pvts[chanpos]->cid_name,
						ast_connected.id.name, sizeof(pri->pvts[chanpos]->cid_name));
					caller_id_update = 1;
				}
				if (ast_connected.id.number) {
					/* Save number for Caller-ID update */
					ast_copy_string(pri->pvts[chanpos]->cid_num, ast_connected.id.number,
						sizeof(pri->pvts[chanpos]->cid_num));
					pri->pvts[chanpos]->cid_ton = ast_connected.id.number_type;
					caller_id_update = 1;
				} else {
					ast_connected.id.number = ast_strdup("");
				}
				ast_connected.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;

				pri->pvts[chanpos]->cid_subaddr[0] = '\0';
#if defined(HAVE_PRI_SUBADDR)
				if (ast_connected.id.subaddress.valid) {
					ast_party_subaddress_set(&owner->cid.subaddress,
						&ast_connected.id.subaddress);
					if (ast_connected.id.subaddress.str) {
						ast_copy_string(pri->pvts[chanpos]->cid_subaddr,
							ast_connected.id.subaddress.str,
							sizeof(pri->pvts[chanpos]->cid_subaddr));
					}
				}
#endif	/* defined(HAVE_PRI_SUBADDR) */
				if (caller_id_update) {
					pri->pvts[chanpos]->callingpres =
						ast_connected.id.number_presentation;
					sig_pri_set_caller_id(pri->pvts[chanpos]);
					ast_set_callerid(owner, S_OR(ast_connected.id.number, NULL),
						S_OR(ast_connected.id.name, NULL),
						S_OR(ast_connected.id.number, NULL));
				}

				/* Update the connected line information on the other channel */
				if (event_id != PRI_EVENT_RING) {
					/* This connected_line update was not from a SETUP message. */
					ast_channel_queue_connected_line_update(owner, &ast_connected);
				}

				ast_party_connected_line_free(&ast_connected);
				ast_channel_unlock(owner);
			}
			break;
		case PRI_SUBCMD_REDIRECTING:
			sig_pri_lock_owner(pri, chanpos);
			owner = pri->pvts[chanpos]->owner;
			if (owner) {
				sig_pri_redirecting_convert(&ast_redirecting, &subcmd->u.redirecting,
					&owner->redirecting, pri);

/*! \todo XXX Original called data can be put in a channel data store that is inherited. */

				ast_channel_set_redirecting(owner, &ast_redirecting);
				if (event_id != PRI_EVENT_RING) {
					/* This redirection was not from a SETUP message. */
					ast_channel_queue_redirecting_update(owner, &ast_redirecting);
				}
				ast_party_redirecting_free(&ast_redirecting);

				ast_channel_unlock(owner);
			}
			break;
#if defined(HAVE_PRI_CALL_REROUTING)
		case PRI_SUBCMD_REROUTING:
			sig_pri_lock_owner(pri, chanpos);
			owner = pri->pvts[chanpos]->owner;
			if (owner) {
				struct pri_party_redirecting pri_deflection;

				if (!call_rsp) {
					ast_channel_unlock(owner);
					ast_log(LOG_WARNING,
						"CallRerouting/CallDeflection to '%s' without call!\n",
						subcmd->u.rerouting.deflection.to.number.str);
					break;
				}

				pri_deflection = subcmd->u.rerouting.deflection;

				ast_string_field_set(owner, call_forward, pri_deflection.to.number.str);

				/* Adjust the deflecting to number based upon the subscription option. */
				switch (subcmd->u.rerouting.subscription_option) {
				case 0:	/* noNotification */
				case 1:	/* notificationWithoutDivertedToNr */
					/* Delete the number because the far end is not supposed to see it. */
					pri_deflection.to.number.presentation =
						PRI_PRES_RESTRICTED | PRI_PRES_USER_NUMBER_UNSCREENED;
					pri_deflection.to.number.plan =
						(PRI_TON_UNKNOWN << 4) | PRI_NPI_E163_E164;
					pri_deflection.to.number.str[0] = '\0';
					break;
				case 2:	/* notificationWithDivertedToNr */
					break;
				case 3:	/* notApplicable */
				default:
					break;
				}
				sig_pri_redirecting_convert(&ast_redirecting, &pri_deflection,
					&owner->redirecting, pri);
				ast_channel_set_redirecting(owner, &ast_redirecting);
				ast_party_redirecting_free(&ast_redirecting);

				/*
				 * Send back positive ACK to CallRerouting/CallDeflection.
				 *
				 * Note:  This call will be hungup by the dial application when
				 * it processes the call_forward string set above.
				 */
				pri_rerouting_rsp(pri->pri, call_rsp, subcmd->u.rerouting.invoke_id,
					PRI_REROUTING_RSP_OK_CLEAR);

				/* This line is BUSY to further attempts by this dialing attempt. */
				ast_queue_control(owner, AST_CONTROL_BUSY);

				ast_channel_unlock(owner);
			}
			break;
#endif	/* defined(HAVE_PRI_CALL_REROUTING) */
		default:
			ast_debug(2,
				"Unknown call subcommand(%d) in %s event on channel %d/%d on span %d.\n",
				subcmd->cmd, pri_event2str(event_id), PRI_SPAN(channel),
				PRI_CHANNEL(channel), pri->span);
			break;
		}
	}
}

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief Attempt to transfer the active call to the held call.
 * \since 1.8
 *
 * \param pri sig_pri PRI control structure.
 * \param active_call Active call to transfer.
 * \param held_call Held call to transfer.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int sig_pri_attempt_transfer(struct sig_pri_pri *pri, q931_call *active_call, q931_call *held_call)
{
	int retval;
	int active_chanpos;
	int held_chanpos;
	struct ast_channel *active_ast;
	struct ast_channel *held_ast;
	struct ast_channel *bridged;

	active_chanpos = pri_find_pri_call(pri, active_call);
	held_chanpos = pri_find_pri_call(pri, held_call);
	if (active_chanpos < 0 || held_chanpos < 0) {
		return -1;
	}

	sig_pri_lock_private(pri->pvts[active_chanpos]);
	sig_pri_lock_private(pri->pvts[held_chanpos]);
	sig_pri_lock_owner(pri, active_chanpos);
	sig_pri_lock_owner(pri, held_chanpos);

	active_ast = pri->pvts[active_chanpos]->owner;
	held_ast = pri->pvts[held_chanpos]->owner;
	if (!active_ast || !held_ast) {
		if (active_ast) {
			ast_channel_unlock(active_ast);
		}
		if (held_ast) {
			ast_channel_unlock(held_ast);
		}
		sig_pri_unlock_private(pri->pvts[active_chanpos]);
		sig_pri_unlock_private(pri->pvts[held_chanpos]);
		return -1;
	}

	bridged = ast_bridged_channel(held_ast);
	if (bridged) {
		ast_queue_control(held_ast, AST_CONTROL_UNHOLD);

		ast_verb(3, "TRANSFERRING %s to %s\n", held_ast->name, active_ast->name);
		retval = ast_channel_masquerade(active_ast, bridged);
	} else {
		/*
		 * Could not transfer.  Held channel is not bridged anymore.
		 * Held party probably got tired of waiting and hung up.
		 */
		retval = -1;
	}

	ast_channel_unlock(active_ast);
	ast_channel_unlock(held_ast);
	sig_pri_unlock_private(pri->pvts[active_chanpos]);
	sig_pri_unlock_private(pri->pvts[held_chanpos]);

	return retval;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief Handle the hold event from libpri.
 * \since 1.8
 *
 * \param pri sig_pri PRI control structure.
 * \param ev Hold event received.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int sig_pri_handle_hold(struct sig_pri_pri *pri, pri_event *ev)
{
	int retval;
	int chanpos_old;
	int chanpos_new;
	struct ast_channel *bridged;
	struct ast_channel *owner;

	chanpos_old = pri_find_principle(pri, ev->hold.channel, ev->hold.call);
	if (chanpos_old < 0) {
		ast_log(LOG_WARNING,
			"Received HOLD on unconfigured channel %d/%d span %d\n",
			PRI_SPAN(ev->hold.channel), PRI_CHANNEL(ev->hold.channel), pri->span);
		return -1;
	}
	if (pri->pvts[chanpos_old]->no_b_channel) {
		/* Call is already on hold or is call waiting call. */
		return -1;
	}

	sig_pri_lock_private(pri->pvts[chanpos_old]);
	sig_pri_lock_owner(pri, chanpos_old);
	owner = pri->pvts[chanpos_old]->owner;
	if (!owner) {
		retval = -1;
		goto done_with_private;
	}
	bridged = ast_bridged_channel(owner);
	if (!bridged) {
		/* Cannot hold a call that is not bridged. */
		retval = -1;
		goto done_with_owner;
	}
	chanpos_new = pri_find_empty_nobch(pri);
	if (chanpos_new < 0) {
		/* No hold channel available. */
		retval = -1;
		goto done_with_owner;
	}
	sig_pri_handle_subcmds(pri, chanpos_old, ev->e, ev->hold.channel, ev->hold.subcmds,
		ev->hold.call);
	chanpos_new = pri_fixup_principle(pri, chanpos_new, ev->hold.call);
	if (chanpos_new < 0) {
		/* Should never happen. */
		retval = -1;
	} else {
		struct ast_frame f = { AST_FRAME_CONTROL, };

		f.subclass.integer = AST_CONTROL_HOLD;
		ast_queue_frame(owner, &f);
		retval = 0;
	}

done_with_owner:;
	ast_channel_unlock(owner);
done_with_private:;
	sig_pri_unlock_private(pri->pvts[chanpos_old]);

	return retval;
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

#if defined(HAVE_PRI_CALL_HOLD)
/*!
 * \internal
 * \brief Handle the retrieve event from libpri.
 * \since 1.8
 *
 * \param pri sig_pri PRI control structure.
 * \param ev Retrieve event received.
 *
 * \note Assumes the pri->lock is already obtained.
 *
 * \return Nothing
 */
static void sig_pri_handle_retrieve(struct sig_pri_pri *pri, pri_event *ev)
{
	int chanpos;

	if (!(ev->retrieve.channel & PRI_HELD_CALL)
		|| pri_find_principle(pri, ev->retrieve.channel, ev->retrieve.call) < 0) {
		/* The call is not currently held. */
		pri_retrieve_rej(pri->pri, ev->retrieve.call,
			PRI_CAUSE_RESOURCE_UNAVAIL_UNSPECIFIED);
		return;
	}
	if (PRI_CHANNEL(ev->retrieve.channel) == 0xFF) {
		chanpos = pri_find_empty_chan(pri, 1);
	} else {
		chanpos = pri_find_principle(pri,
			ev->retrieve.channel & ~PRI_HELD_CALL, ev->retrieve.call);
		if (ev->retrieve.flexible
			&& (chanpos < 0 || pri->pvts[chanpos]->owner)) {
			/*
			 * Channel selection is flexible and the requested channel
			 * is bad or already in use.  Pick another channel.
			 */
			chanpos = pri_find_empty_chan(pri, 1);
		}
	}
	if (chanpos < 0) {
		pri_retrieve_rej(pri->pri, ev->retrieve.call,
			ev->retrieve.flexible ? PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION
			: PRI_CAUSE_REQUESTED_CHAN_UNAVAIL);
		return;
	}
	chanpos = pri_fixup_principle(pri, chanpos, ev->retrieve.call);
	if (chanpos < 0) {
		/* Channel is already in use. */
		pri_retrieve_rej(pri->pri, ev->retrieve.call,
			PRI_CAUSE_REQUESTED_CHAN_UNAVAIL);
		return;
	}
	sig_pri_lock_private(pri->pvts[chanpos]);
	sig_pri_handle_subcmds(pri, chanpos, ev->e, ev->retrieve.channel,
		ev->retrieve.subcmds, ev->retrieve.call);
	{
		struct ast_frame f = { AST_FRAME_CONTROL, };

		f.subclass.integer = AST_CONTROL_UNHOLD;
		pri_queue_frame(pri->pvts[chanpos], &f, pri);
	}
	sig_pri_unlock_private(pri->pvts[chanpos]);
	pri_retrieve_ack(pri->pri, ev->retrieve.call,
		PVT_TO_CHANNEL(pri->pvts[chanpos]));
}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */

static void *pri_dchannel(void *vpri)
{
	struct sig_pri_pri *pri = vpri;
	pri_event *e;
	struct pollfd fds[NUM_DCHANS];
	int res;
	int chanpos = 0;
	int x;
	struct ast_channel *c;
	struct timeval tv, lowest, *next;
	int doidling=0;
	char *cc;
	time_t t;
	int i, which=-1;
	int numdchans;
	pthread_t threadid;
	char ani2str[6];
	char plancallingnum[AST_MAX_EXTENSION];
	char plancallingani[AST_MAX_EXTENSION];
	char calledtonstr[10];
	struct timeval lastidle = { 0, 0 };
	pthread_t p;
	struct ast_channel *idle;
	char idlen[80];
	int nextidle = -1;
	int haveidles;
	int activeidles;

	gettimeofday(&lastidle, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	if (!ast_strlen_zero(pri->idledial) && !ast_strlen_zero(pri->idleext)) {
		/* Need to do idle dialing, check to be sure though */
		cc = strchr(pri->idleext, '@');
		if (cc) {
			*cc = '\0';
			cc++;
			ast_copy_string(pri->idlecontext, cc, sizeof(pri->idlecontext));
#if 0
			/* Extensions may not be loaded yet */
			if (!ast_exists_extension(NULL, pri->idlecontext, pri->idleext, 1, NULL))
				ast_log(LOG_WARNING, "Extension '%s @ %s' does not exist\n", pri->idleext, pri->idlecontext);
			else
#endif
				doidling = 1;
		} else
			ast_log(LOG_WARNING, "Idle dial string '%s' lacks '@context'\n", pri->idleext);
	}
	for (;;) {
		for (i = 0; i < NUM_DCHANS; i++) {
			if (!pri->dchans[i])
				break;
			fds[i].fd = pri->fds[i];
			fds[i].events = POLLIN | POLLPRI;
			fds[i].revents = 0;
		}
		numdchans = i;
		time(&t);
		ast_mutex_lock(&pri->lock);
		if (pri->switchtype != PRI_SWITCH_GR303_TMC && (pri->sig != SIG_BRI_PTMP) && (pri->resetinterval > 0)) {
			if (pri->resetting && pri_is_up(pri)) {
				if (pri->resetpos < 0)
					pri_check_restart(pri);
			} else {
				if (!pri->resetting	&& (t - pri->lastreset) >= pri->resetinterval) {
					pri->resetting = 1;
					pri->resetpos = -1;
				}
			}
		}
		/* Look for any idle channels if appropriate */
		if (doidling && pri_is_up(pri)) {
			nextidle = -1;
			haveidles = 0;
			activeidles = 0;
			for (x = pri->numchans; x >= 0; x--) {
				if (pri->pvts[x]
					&& !pri->pvts[x]->owner
					&& !pri->pvts[x]->call
					&& !pri->pvts[x]->no_b_channel) {
					if (haveidles < pri->minunused) {
						haveidles++;
					} else if (!pri->pvts[x]->resetting) {
						nextidle = x;
						break;
					}
				} else if (pri->pvts[x] && pri->pvts[x]->owner && pri->pvts[x]->isidlecall)
					activeidles++;
			}
			if (nextidle > -1) {
				if (ast_tvdiff_ms(ast_tvnow(), lastidle) > 1000) {
					/* Don't create a new idle call more than once per second */
					snprintf(idlen, sizeof(idlen), "%d/%s", pri->pvts[nextidle]->channel, pri->idledial);
					idle = sig_pri_request(pri->pvts[nextidle], AST_FORMAT_ULAW, NULL, 0);
					if (idle) {
						pri->pvts[nextidle]->isidlecall = 1;
						if (ast_pthread_create_background(&p, NULL, do_idle_thread, pri->pvts[nextidle])) {
							ast_log(LOG_WARNING, "Unable to start new thread for idle channel '%s'\n", idle->name);
							ast_hangup(idle);
						}
					} else
						ast_log(LOG_WARNING, "Unable to request channel 'DAHDI/%s' for idle call\n", idlen);
					gettimeofday(&lastidle, NULL);
				}
			} else if ((haveidles < pri->minunused) &&
				(activeidles > pri->minidle)) {
				/* Mark something for hangup if there is something
				   that can be hungup */
				for (x = pri->numchans; x >= 0; x--) {
					/* find a candidate channel */
					if (pri->pvts[x] && pri->pvts[x]->owner && pri->pvts[x]->isidlecall) {
						pri->pvts[x]->owner->_softhangup |= AST_SOFTHANGUP_DEV;
						haveidles++;
						/* Stop if we have enough idle channels or
						  can't spare any more active idle ones */
						if ((haveidles >= pri->minunused) ||
							(activeidles <= pri->minidle))
							break;
					}
				}
			}
		}
		/* Start with reasonable max */
		lowest = ast_tv(60, 0);
		for (i = 0; i < NUM_DCHANS; i++) {
			/* Find lowest available d-channel */
			if (!pri->dchans[i])
				break;
			if ((next = pri_schedule_next(pri->dchans[i]))) {
				/* We need relative time here */
				tv = ast_tvsub(*next, ast_tvnow());
				if (tv.tv_sec < 0) {
					tv = ast_tv(0,0);
				}
				if (doidling || pri->resetting) {
					if (tv.tv_sec > 1) {
						tv = ast_tv(1, 0);
					}
				} else {
					if (tv.tv_sec > 60) {
						tv = ast_tv(60, 0);
					}
				}
			} else if (doidling || pri->resetting) {
				/* Make sure we stop at least once per second if we're
				   monitoring idle channels */
				tv = ast_tv(1,0);
			} else {
				/* Don't poll for more than 60 seconds */
				tv = ast_tv(60, 0);
			}
			if (!i || ast_tvcmp(tv, lowest) < 0) {
				lowest = tv;
			}
		}
		ast_mutex_unlock(&pri->lock);

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		pthread_testcancel();
		e = NULL;
		res = poll(fds, numdchans, lowest.tv_sec * 1000 + lowest.tv_usec / 1000);
		pthread_testcancel();
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		ast_mutex_lock(&pri->lock);
		if (!res) {
			for (which = 0; which < NUM_DCHANS; which++) {
				if (!pri->dchans[which])
					break;
				/* Just a timeout, run the scheduler */
				e = pri_schedule_run(pri->dchans[which]);
				if (e)
					break;
			}
		} else if (res > -1) {
			for (which = 0; which < NUM_DCHANS; which++) {
				if (!pri->dchans[which])
					break;
				if (fds[which].revents & POLLPRI) {
					sig_pri_handle_dchan_exception(pri, which);
				} else if (fds[which].revents & POLLIN) {
					e = pri_check_event(pri->dchans[which]);
				}
				if (e)
					break;
			}
		} else if (errno != EINTR)
			ast_log(LOG_WARNING, "pri_event returned error %d (%s)\n", errno, strerror(errno));

		if (e) {
			if (pri->debug)
				pri_dump_event(pri->dchans[which], e);

			if (e->e != PRI_EVENT_DCHAN_DOWN) {
				if (!(pri->dchanavail[which] & DCHAN_UP)) {
					ast_verb(2, "%s D-Channel on span %d up\n", pri_order(which), pri->span);
				}
				pri->dchanavail[which] |= DCHAN_UP;
			} else {
				if (pri->dchanavail[which] & DCHAN_UP) {
					ast_verb(2, "%s D-Channel on span %d down\n", pri_order(which), pri->span);
				}
				pri->dchanavail[which] &= ~DCHAN_UP;
			}

			if ((e->e != PRI_EVENT_DCHAN_UP) && (e->e != PRI_EVENT_DCHAN_DOWN) && (pri->pri != pri->dchans[which]))
				/* Must be an NFAS group that has the secondary dchan active */
				pri->pri = pri->dchans[which];

			switch (e->e) {
			case PRI_EVENT_DCHAN_UP:
				if (!pri->pri) pri_find_dchan(pri);

				/* Note presense of D-channel */
				time(&pri->lastreset);

				/* Restart in 5 seconds */
				if (pri->resetinterval > -1) {
					pri->lastreset -= pri->resetinterval;
					pri->lastreset += 5;
				}
				pri->resetting = 0;
				/* Take the channels from inalarm condition */
				for (i = 0; i < pri->numchans; i++)
					if (pri->pvts[i]) {
						pri->pvts[i]->inalarm = 0;
					}
				break;
			case PRI_EVENT_DCHAN_DOWN:
				pri_find_dchan(pri);
				if (!pri_is_up(pri)) {
					pri->resetting = 0;
					if (pri->sig == SIG_BRI_PTMP) {
						/* For PTMP connections with non persistent layer 2 we want
						 * to *not* declare inalarm unless there actually is an alarm */
						break;
					}
					/* Hangup active channels and put them in alarm mode */
					for (i = 0; i < pri->numchans; i++) {
						struct sig_pri_chan *p = pri->pvts[i];
						if (p) {
							if (!p->pri || !p->pri->pri || pri_get_timer(p->pri->pri, PRI_TIMER_T309) < 0) {
								/* T309 is not enabled : hangup calls when alarm occurs */
								if (p->call) {
									if (p->pri && p->pri->pri) {
										pri_hangup(p->pri->pri, p->call, -1);
										pri_destroycall(p->pri->pri, p->call);
										p->call = NULL;
									} else
										ast_log(LOG_WARNING, "The PRI Call have not been destroyed\n");
								}
								if (p->owner)
									ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_DEV);
							}
							p->inalarm = 1;
						}
					}
				}
				break;
			case PRI_EVENT_RESTART:
				if (e->restart.channel > -1) {
					chanpos = pri_find_principle(pri, e->restart.channel, NULL);
					if (chanpos < 0)
						ast_log(LOG_WARNING, "Restart requested on odd/unavailable channel number %d/%d on span %d\n",
							PRI_SPAN(e->restart.channel), PRI_CHANNEL(e->restart.channel), pri->span);
					else {
						int skipit = 0;
#if defined(HAVE_PRI_SERVICE_MESSAGES)
						unsigned why;

						why = pri->pvts[chanpos]->service_status;
						if (why) {
							ast_log(LOG_NOTICE,
								"span '%d' channel '%d' out-of-service (reason: %s), ignoring RESTART\n",
								pri->span, PRI_CHANNEL(e->restart.channel),
								(why & SRVST_FAREND) ? (why & SRVST_NEAREND) ? "both ends" : "far end" : "near end");
							skipit = 1;
						}
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
						sig_pri_lock_private(pri->pvts[chanpos]);
						if (!skipit) {
							ast_verb(3, "B-channel %d/%d restarted on span %d\n",
								PRI_SPAN(e->restart.channel), PRI_CHANNEL(e->restart.channel), pri->span);
							if (pri->pvts[chanpos]->call) {
								pri_destroycall(pri->pri, pri->pvts[chanpos]->call);
								pri->pvts[chanpos]->call = NULL;
							}
						}
						/* Force soft hangup if appropriate */
						if (pri->pvts[chanpos]->owner)
							ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				} else {
					ast_verb(3, "Restart requested on entire span %d\n", pri->span);
					for (x = 0; x < pri->numchans; x++)
						if (pri->pvts[x]) {
							sig_pri_lock_private(pri->pvts[x]);
							if (pri->pvts[x]->call) {
								pri_destroycall(pri->pri, pri->pvts[x]->call);
								pri->pvts[x]->call = NULL;
							}
 							if (pri->pvts[x]->owner)
								ast_softhangup_nolock(pri->pvts[x]->owner, AST_SOFTHANGUP_DEV);
							sig_pri_unlock_private(pri->pvts[x]);
						}
				}
				break;
			case PRI_EVENT_KEYPAD_DIGIT:
				chanpos = pri_find_principle(pri, e->digit.channel, e->digit.call);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "KEYPAD_DIGITs received on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->digit.channel), PRI_CHANNEL(e->digit.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->digit.call);
					if (chanpos > -1) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						sig_pri_handle_subcmds(pri, chanpos, e->e, e->digit.channel,
							e->digit.subcmds, e->digit.call);
						/* queue DTMF frame if the PBX for this call was already started (we're forwarding KEYPAD_DIGITs further on */
						if ((pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)
							&& pri->pvts[chanpos]->call == e->digit.call
							&& pri->pvts[chanpos]->owner) {
							/* how to do that */
							int digitlen = strlen(e->digit.digits);
							int i;

							for (i = 0; i < digitlen; i++) {
								struct ast_frame f = { AST_FRAME_DTMF, .subclass.integer = e->digit.digits[i], };

								pri_queue_frame(pri->pvts[chanpos], &f, pri);
							}
						}
						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				}
				break;

			case PRI_EVENT_INFO_RECEIVED:
				chanpos = pri_find_principle(pri, e->ring.channel, e->ring.call);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "INFO received on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->ring.call);
					if (chanpos > -1) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						sig_pri_handle_subcmds(pri, chanpos, e->e, e->ring.channel,
							e->ring.subcmds, e->ring.call);
						/* queue DTMF frame if the PBX for this call was already started (we're forwarding INFORMATION further on */
						if ((pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)
							&& pri->pvts[chanpos]->call == e->ring.call
							&& pri->pvts[chanpos]->owner) {
							/* how to do that */
							int digitlen = strlen(e->ring.callednum);
							int i;

							for (i = 0; i < digitlen; i++) {
								struct ast_frame f = { AST_FRAME_DTMF, .subclass.integer = e->ring.callednum[i], };

								pri_queue_frame(pri->pvts[chanpos], &f, pri);
							}
						}
						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				}
				break;
#if defined(HAVE_PRI_SERVICE_MESSAGES)
			case PRI_EVENT_SERVICE:
				chanpos = pri_find_principle(pri, e->service.channel, NULL);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received service change status %d on unconfigured channel %d/%d span %d\n",
						e->service_ack.changestatus, PRI_SPAN(e->service_ack.channel), PRI_CHANNEL(e->service_ack.channel), pri->span);
				} else {
					char db_chan_name[20];
					char db_answer[5];
					int ch;
					unsigned *why;

					ch = pri->pvts[chanpos]->channel;
					snprintf(db_chan_name, sizeof(db_chan_name), "%s/%d:%d", dahdi_db, pri->span, ch);
					why = &pri->pvts[chanpos]->service_status;
					switch (e->service.changestatus) {
					case 0: /* in-service */
						/* Far end wants to be in service now. */
						ast_db_del(db_chan_name, SRVST_DBKEY);
						*why &= ~SRVST_FAREND;
						if (*why) {
							snprintf(db_answer, sizeof(db_answer), "%s:%u",
								SRVST_TYPE_OOS, *why);
							ast_db_put(db_chan_name, SRVST_DBKEY, db_answer);
						}
						break;
					case 2: /* out-of-service */
						/* Far end wants to be out-of-service now. */
						ast_db_del(db_chan_name, SRVST_DBKEY);
						*why |= SRVST_FAREND;
						snprintf(db_answer, sizeof(db_answer), "%s:%u", SRVST_TYPE_OOS,
							*why);
						ast_db_put(db_chan_name, SRVST_DBKEY, db_answer);
						break;
					default:
						ast_log(LOG_ERROR, "Huh?  changestatus is: %d\n", e->service.changestatus);
						break;
					}
					ast_log(LOG_NOTICE, "Channel %d/%d span %d (logical: %d) received a change of service message, status '%d'\n",
						PRI_SPAN(e->service.channel), PRI_CHANNEL(e->service.channel), pri->span, ch, e->service.changestatus);
				}
				break;
			case PRI_EVENT_SERVICE_ACK:
				chanpos = pri_find_principle(pri, e->service_ack.channel, NULL);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received service acknowledge change status '%d' on unconfigured channel %d/%d span %d\n",
						e->service_ack.changestatus, PRI_SPAN(e->service_ack.channel), PRI_CHANNEL(e->service_ack.channel), pri->span);
				} else {
					ast_debug(2, "Channel %d/%d span %d received a change os service acknowledgement message, status '%d'\n",
						PRI_SPAN(e->service_ack.channel), PRI_CHANNEL(e->service_ack.channel), pri->span, e->service_ack.changestatus);
				}
				break;
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
			case PRI_EVENT_RING:
				if (!ast_strlen_zero(pri->msn_list)
					&& !sig_pri_msn_match(pri->msn_list, e->ring.callednum)) {
					/* The call is not for us so ignore it. */
					ast_verb(3,
						"Ignoring call to '%s' on span %d.  Its not in the MSN list: %s\n",
						e->ring.callednum, pri->span, pri->msn_list);
					pri_destroycall(pri->pri, e->ring.call);
					break;
				}
				if (e->ring.channel == -1)
					chanpos = pri_find_empty_chan(pri, 1);
				else
					chanpos = pri_find_principle(pri, e->ring.channel, e->ring.call);
				/* if no channel specified find one empty */
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Ring requested on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
				} else {
					sig_pri_lock_private(pri->pvts[chanpos]);
					if (pri->pvts[chanpos]->owner) {
						if (pri->pvts[chanpos]->call == e->ring.call) {
							ast_log(LOG_WARNING, "Duplicate setup requested on channel %d/%d already in use on span %d\n",
								PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
							sig_pri_unlock_private(pri->pvts[chanpos]);
							break;
						} else {
							/* This is where we handle initial glare */
							ast_debug(1, "Ring requested on channel %d/%d already in use or previously requested on span %d.  Attempting to renegotiating channel.\n",
							PRI_SPAN(e->ring.channel), PRI_CHANNEL(e->ring.channel), pri->span);
							sig_pri_unlock_private(pri->pvts[chanpos]);
							chanpos = -1;
						}
					}
					if (chanpos > -1)
						sig_pri_unlock_private(pri->pvts[chanpos]);
				}
				if ((chanpos < 0) && (e->ring.flexible))
					chanpos = pri_find_empty_chan(pri, 1);
				if (chanpos > -1) {
					sig_pri_lock_private(pri->pvts[chanpos]);
					pri->pvts[chanpos]->call = e->ring.call;

					/* Use plancallingnum as a scratch buffer since it is initialized next. */
					apply_plan_to_number(plancallingnum, sizeof(plancallingnum), pri,
						e->ring.redirectingnum, e->ring.callingplanrdnis);
					sig_pri_set_rdnis(pri->pvts[chanpos], plancallingnum);

					/* Setup caller-id info */
					apply_plan_to_number(plancallingnum, sizeof(plancallingnum), pri, e->ring.callingnum, e->ring.callingplan);
					pri->pvts[chanpos]->cid_ani2 = 0;
					if (pri->pvts[chanpos]->use_callerid) {
						ast_shrink_phone_number(plancallingnum);
						ast_copy_string(pri->pvts[chanpos]->cid_num, plancallingnum, sizeof(pri->pvts[chanpos]->cid_num));
#ifdef PRI_ANI
						if (!ast_strlen_zero(e->ring.callingani)) {
							apply_plan_to_number(plancallingani, sizeof(plancallingani), pri, e->ring.callingani, e->ring.callingplanani);
							ast_shrink_phone_number(plancallingani);
							ast_copy_string(pri->pvts[chanpos]->cid_ani, plancallingani, sizeof(pri->pvts[chanpos]->cid_ani));
						} else {
							pri->pvts[chanpos]->cid_ani[0] = '\0';
						}
#endif
						pri->pvts[chanpos]->cid_subaddr[0] = '\0';
#if defined(HAVE_PRI_SUBADDR)
						if (e->ring.calling.subaddress.valid) {
							struct ast_party_subaddress calling_subaddress;

							ast_party_subaddress_init(&calling_subaddress);
							sig_pri_set_subaddress(&calling_subaddress,
								&e->ring.calling.subaddress);
							if (calling_subaddress.str) {
								ast_copy_string(pri->pvts[chanpos]->cid_subaddr,
									calling_subaddress.str,
									sizeof(pri->pvts[chanpos]->cid_subaddr));
							}
							ast_party_subaddress_free(&calling_subaddress);
						}
#endif /* defined(HAVE_PRI_SUBADDR) */
						ast_copy_string(pri->pvts[chanpos]->cid_name, e->ring.callingname, sizeof(pri->pvts[chanpos]->cid_name));
						pri->pvts[chanpos]->cid_ton = e->ring.callingplan; /* this is the callingplan (TON/NPI), e->ring.callingplan>>4 would be the TON */
						pri->pvts[chanpos]->callingpres = e->ring.callingpres;
						if (e->ring.ani2 >= 0) {
							pri->pvts[chanpos]->cid_ani2 = e->ring.ani2;
						}
					} else {
						pri->pvts[chanpos]->cid_num[0] = '\0';
						pri->pvts[chanpos]->cid_subaddr[0] = '\0';
						pri->pvts[chanpos]->cid_ani[0] = '\0';
						pri->pvts[chanpos]->cid_name[0] = '\0';
						pri->pvts[chanpos]->cid_ton = 0;
						pri->pvts[chanpos]->callingpres = 0;
					}
					sig_pri_set_caller_id(pri->pvts[chanpos]);

					/* Set DNID on all incoming calls -- even immediate */
					sig_pri_set_dnid(pri->pvts[chanpos], e->ring.callednum);

					/* If immediate=yes go to s|1 */
					if (pri->pvts[chanpos]->immediate) {
						ast_verb(3, "Going to extension s|1 because of immediate=yes\n");
						pri->pvts[chanpos]->exten[0] = 's';
						pri->pvts[chanpos]->exten[1] = '\0';
					}
					/* Get called number */
					else if (!ast_strlen_zero(e->ring.callednum)) {
						ast_copy_string(pri->pvts[chanpos]->exten, e->ring.callednum, sizeof(pri->pvts[chanpos]->exten));
					} else if (pri->overlapdial)
						pri->pvts[chanpos]->exten[0] = '\0';
					else {
						/* Some PRI circuits are set up to send _no_ digits.  Handle them as 's'. */
						pri->pvts[chanpos]->exten[0] = 's';
						pri->pvts[chanpos]->exten[1] = '\0';
					}
					/* No number yet, but received "sending complete"? */
					if (e->ring.complete && (ast_strlen_zero(e->ring.callednum))) {
						ast_verb(3, "Going to extension s|1 because of Complete received\n");
						pri->pvts[chanpos]->exten[0] = 's';
						pri->pvts[chanpos]->exten[1] = '\0';
					}

					/* Make sure extension exists (or in overlap dial mode, can exist) */
					if (((pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING) && ast_canmatch_extension(NULL, pri->pvts[chanpos]->context, pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num)) ||
						ast_exists_extension(NULL, pri->pvts[chanpos]->context, pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num)) {
						/* Setup law */
						if (e->ring.complete || !(pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)) {
							/* Just announce proceeding */
							pri->pvts[chanpos]->proceeding = 1;
							pri_proceeding(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 0);
						} else {
							if (pri->switchtype != PRI_SWITCH_GR303_TMC)
								pri_need_more_info(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 1);
							else
								pri_answer(pri->pri, e->ring.call, PVT_TO_CHANNEL(pri->pvts[chanpos]), 1);
						}

						/* Start PBX */
						if (!e->ring.complete
							&& (pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)
							&& ast_matchmore_extension(NULL, pri->pvts[chanpos]->context, pri->pvts[chanpos]->exten, 1, pri->pvts[chanpos]->cid_num)) {
							/*
							 * Release the PRI lock while we create the channel
							 * so other threads can send D channel messages.
							 */
							ast_mutex_unlock(&pri->lock);
							c = sig_pri_new_ast_channel(pri->pvts[chanpos],
								AST_STATE_RESERVED, 0,
								(e->ring.layer1 == PRI_LAYER_1_ALAW)
									? SIG_PRI_ALAW : SIG_PRI_ULAW,
								e->ring.ctype, pri->pvts[chanpos]->exten, NULL);
							ast_mutex_lock(&pri->lock);
							if (c) {
#if defined(HAVE_PRI_SUBADDR)
								if (e->ring.calling.subaddress.valid) {
									/* Set Calling Subaddress */
									sig_pri_lock_owner(pri, chanpos);
									sig_pri_set_subaddress(
										&pri->pvts[chanpos]->owner->cid.subaddress,
										&e->ring.calling.subaddress);
									if (!e->ring.calling.subaddress.type
										&& !ast_strlen_zero(
											(char *) e->ring.calling.subaddress.data)) {
										/* NSAP */
										pbx_builtin_setvar_helper(c, "CALLINGSUBADDR",
											(char *) e->ring.calling.subaddress.data);
									}
									ast_channel_unlock(c);
								}
								if (e->ring.called_subaddress.valid) {
									/* Set Called Subaddress */
									sig_pri_lock_owner(pri, chanpos);
									sig_pri_set_subaddress(
										&pri->pvts[chanpos]->owner->cid.dialed_subaddress,
										&e->ring.called_subaddress);
									if (!e->ring.called_subaddress.type
										&& !ast_strlen_zero(
											(char *) e->ring.called_subaddress.data)) {
										/* NSAP */
										pbx_builtin_setvar_helper(c, "CALLEDSUBADDR",
											(char *) e->ring.called_subaddress.data);
									}
									ast_channel_unlock(c);
								}
#else
								if (!ast_strlen_zero(e->ring.callingsubaddr)) {
									pbx_builtin_setvar_helper(c, "CALLINGSUBADDR", e->ring.callingsubaddr);
								}
#endif /* !defined(HAVE_PRI_SUBADDR) */
								if (e->ring.ani2 >= 0) {
									snprintf(ani2str, sizeof(ani2str), "%d", e->ring.ani2);
									pbx_builtin_setvar_helper(c, "ANI2", ani2str);
								}

#ifdef SUPPORT_USERUSER
								if (!ast_strlen_zero(e->ring.useruserinfo)) {
									pbx_builtin_setvar_helper(c, "USERUSERINFO", e->ring.useruserinfo);
								}
#endif

								snprintf(calledtonstr, sizeof(calledtonstr), "%d", e->ring.calledplan);
								pbx_builtin_setvar_helper(c, "CALLEDTON", calledtonstr);
								if (e->ring.redirectingreason >= 0) {
									/* This is now just a status variable.  Use REDIRECTING() dialplan function. */
									pbx_builtin_setvar_helper(c, "PRIREDIRECTREASON", redirectingreason2str(e->ring.redirectingreason));
								}
#if defined(HAVE_PRI_REVERSE_CHARGE)
								pri->pvts[chanpos]->reverse_charging_indication = e->ring.reversecharge;
#endif
#if defined(HAVE_PRI_SETUP_KEYPAD)
								ast_copy_string(pri->pvts[chanpos]->keypad_digits,
									e->ring.keypad_digits,
									sizeof(pri->pvts[chanpos]->keypad_digits));
#endif	/* defined(HAVE_PRI_SETUP_KEYPAD) */

								sig_pri_handle_subcmds(pri, chanpos, e->e, e->ring.channel,
									e->ring.subcmds, e->ring.call);

							}
							if (c && !ast_pthread_create_detached(&threadid, NULL, pri_ss_thread, pri->pvts[chanpos])) {
								ast_verb(3, "Accepting overlap call from '%s' to '%s' on channel %d/%d, span %d\n",
									plancallingnum, S_OR(pri->pvts[chanpos]->exten, "<unspecified>"),
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
							} else {
								ast_log(LOG_WARNING, "Unable to start PBX on channel %d/%d, span %d\n",
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
								if (c)
									ast_hangup(c);
								else {
									pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_SWITCH_CONGESTION);
									pri->pvts[chanpos]->call = NULL;
								}
							}
						} else {
							/*
							 * Release the PRI lock while we create the channel
							 * so other threads can send D channel messages.
							 */
							ast_mutex_unlock(&pri->lock);
							c = sig_pri_new_ast_channel(pri->pvts[chanpos],
								AST_STATE_RING, 0,
								(e->ring.layer1 == PRI_LAYER_1_ALAW)
									? SIG_PRI_ALAW : SIG_PRI_ULAW, e->ring.ctype,
								pri->pvts[chanpos]->exten, NULL);
							ast_mutex_lock(&pri->lock);
							if (c) {
								/*
								 * It is reasonably safe to set the following
								 * channel variables while the PRI and DAHDI private
								 * structures are locked.  The PBX has not been
								 * started yet and it is unlikely that any other task
								 * will do anything with the channel we have just
								 * created.
								 */
#if defined(HAVE_PRI_SUBADDR)
								if (e->ring.calling.subaddress.valid) {
									/* Set Calling Subaddress */
									sig_pri_lock_owner(pri, chanpos);
									sig_pri_set_subaddress(
										&pri->pvts[chanpos]->owner->cid.subaddress,
										&e->ring.calling.subaddress);
									if (!e->ring.calling.subaddress.type
										&& !ast_strlen_zero(
											(char *) e->ring.calling.subaddress.data)) {
										/* NSAP */
										pbx_builtin_setvar_helper(c, "CALLINGSUBADDR",
											(char *) e->ring.calling.subaddress.data);
									}
									ast_channel_unlock(c);
								}
								if (e->ring.called_subaddress.valid) {
									/* Set Called Subaddress */
									sig_pri_lock_owner(pri, chanpos);
									sig_pri_set_subaddress(
										&pri->pvts[chanpos]->owner->cid.dialed_subaddress,
										&e->ring.called_subaddress);
									if (!e->ring.called_subaddress.type
										&& !ast_strlen_zero(
											(char *) e->ring.called_subaddress.data)) {
										/* NSAP */
										pbx_builtin_setvar_helper(c, "CALLEDSUBADDR",
											(char *) e->ring.called_subaddress.data);
									}
									ast_channel_unlock(c);
								}
#else
								if (!ast_strlen_zero(e->ring.callingsubaddr)) {
									pbx_builtin_setvar_helper(c, "CALLINGSUBADDR", e->ring.callingsubaddr);
								}
#endif /* !defined(HAVE_PRI_SUBADDR) */
								if (e->ring.ani2 >= 0) {
									snprintf(ani2str, sizeof(ani2str), "%d", e->ring.ani2);
									pbx_builtin_setvar_helper(c, "ANI2", ani2str);
								}

#ifdef SUPPORT_USERUSER
								if (!ast_strlen_zero(e->ring.useruserinfo)) {
									pbx_builtin_setvar_helper(c, "USERUSERINFO", e->ring.useruserinfo);
								}
#endif

								if (e->ring.redirectingreason >= 0) {
									/* This is now just a status variable.  Use REDIRECTING() dialplan function. */
									pbx_builtin_setvar_helper(c, "PRIREDIRECTREASON", redirectingreason2str(e->ring.redirectingreason));
								}
#if defined(HAVE_PRI_REVERSE_CHARGE)
								pri->pvts[chanpos]->reverse_charging_indication = e->ring.reversecharge;
#endif
#if defined(HAVE_PRI_SETUP_KEYPAD)
								ast_copy_string(pri->pvts[chanpos]->keypad_digits,
									e->ring.keypad_digits,
									sizeof(pri->pvts[chanpos]->keypad_digits));
#endif	/* defined(HAVE_PRI_SETUP_KEYPAD) */

								snprintf(calledtonstr, sizeof(calledtonstr), "%d", e->ring.calledplan);
								pbx_builtin_setvar_helper(c, "CALLEDTON", calledtonstr);

								sig_pri_handle_subcmds(pri, chanpos, e->e, e->ring.channel,
									e->ring.subcmds, e->ring.call);

							}
							if (c && !ast_pbx_start(c)) {
								ast_verb(3, "Accepting call from '%s' to '%s' on channel %d/%d, span %d\n",
									plancallingnum, pri->pvts[chanpos]->exten,
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
								sig_pri_set_echocanceller(pri->pvts[chanpos], 1);
							} else {
								ast_log(LOG_WARNING, "Unable to start PBX on channel %d/%d, span %d\n",
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span);
								if (c) {
									ast_hangup(c);
								} else {
									pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_SWITCH_CONGESTION);
									pri->pvts[chanpos]->call = NULL;
								}
							}
						}
					} else {
						ast_verb(3, "Extension '%s' in context '%s' from '%s' does not exist.  Rejecting call on channel %d/%d, span %d\n",
							pri->pvts[chanpos]->exten, pri->pvts[chanpos]->context, pri->pvts[chanpos]->cid_num, pri->pvts[chanpos]->logicalspan,
							pri->pvts[chanpos]->prioffset, pri->span);
						pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_UNALLOCATED);
						pri->pvts[chanpos]->call = NULL;
						pri->pvts[chanpos]->exten[0] = '\0';
					}
					sig_pri_unlock_private(pri->pvts[chanpos]);
				} else {
					if (e->ring.flexible)
						pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION);
					else
						pri_hangup(pri->pri, e->ring.call, PRI_CAUSE_REQUESTED_CHAN_UNAVAIL);
				}
				break;
			case PRI_EVENT_RINGING:
				chanpos = pri_find_principle(pri, e->ringing.channel, e->ringing.call);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Ringing requested on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->ringing.channel), PRI_CHANNEL(e->ringing.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->ringing.call);
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Ringing requested on channel %d/%d not in use on span %d\n",
							PRI_SPAN(e->ringing.channel), PRI_CHANNEL(e->ringing.channel), pri->span);
					} else {
						sig_pri_lock_private(pri->pvts[chanpos]);

						sig_pri_handle_subcmds(pri, chanpos, e->e, e->ringing.channel,
							e->ringing.subcmds, e->ringing.call);
						sig_pri_set_echocanceller(pri->pvts[chanpos], 1);
						pri_queue_control(pri->pvts[chanpos], AST_CONTROL_RINGING, pri);
						pri->pvts[chanpos]->alerting = 1;

#ifdef SUPPORT_USERUSER
						if (!ast_strlen_zero(e->ringing.useruserinfo)) {
							struct ast_channel *owner;

							sig_pri_lock_owner(pri, chanpos);
							owner = pri->pvts[chanpos]->owner;
							if (owner) {
								pbx_builtin_setvar_helper(owner, "USERUSERINFO",
									e->ringing.useruserinfo);
								ast_channel_unlock(owner);
							}
						}
#endif

						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				}
				break;
			case PRI_EVENT_PROGRESS:
				/* Get chan value if e->e is not PRI_EVNT_RINGING */
				chanpos = pri_find_principle(pri, e->proceeding.channel, e->proceeding.call);
				if (chanpos > -1) {
					sig_pri_lock_private(pri->pvts[chanpos]);
					sig_pri_handle_subcmds(pri, chanpos, e->e, e->proceeding.channel,
						e->proceeding.subcmds, e->proceeding.call);
					if ((!pri->pvts[chanpos]->progress)
#ifdef PRI_PROGRESS_MASK
						|| (e->proceeding.progressmask & PRI_PROG_INBAND_AVAILABLE)
#else
						|| (e->proceeding.progress == 8)
#endif
						) {
						struct ast_frame f = { AST_FRAME_CONTROL, .subclass.integer = AST_CONTROL_PROGRESS, };

						if (e->proceeding.cause > -1) {
							ast_verb(3, "PROGRESS with cause code %d received\n", e->proceeding.cause);

							/* Work around broken, out of spec USER_BUSY cause in a progress message */
							if (e->proceeding.cause == AST_CAUSE_USER_BUSY) {
								if (pri->pvts[chanpos]->owner) {
									ast_verb(3, "PROGRESS with 'user busy' received, signaling AST_CONTROL_BUSY instead of AST_CONTROL_PROGRESS\n");

									pri->pvts[chanpos]->owner->hangupcause = e->proceeding.cause;
									f.subclass.integer = AST_CONTROL_BUSY;
								}
							}
						}

						ast_debug(1, "Queuing frame from PRI_EVENT_PROGRESS on channel %d/%d span %d\n",
							pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset,pri->span);
						pri_queue_frame(pri->pvts[chanpos], &f, pri);
						if (
#ifdef PRI_PROGRESS_MASK
							e->proceeding.progressmask & PRI_PROG_INBAND_AVAILABLE
#else
							e->proceeding.progress == 8
#endif
							) {
							/* Bring voice path up */
							f.subclass.integer = AST_CONTROL_PROGRESS;
							pri_queue_frame(pri->pvts[chanpos], &f, pri);
						}
						pri->pvts[chanpos]->progress = 1;
						sig_pri_set_dialing(pri->pvts[chanpos], 0);
					}
					sig_pri_unlock_private(pri->pvts[chanpos]);
				}
				break;
			case PRI_EVENT_PROCEEDING:
				chanpos = pri_find_principle(pri, e->proceeding.channel, e->proceeding.call);
				if (chanpos > -1) {
					sig_pri_lock_private(pri->pvts[chanpos]);
					sig_pri_handle_subcmds(pri, chanpos, e->e, e->proceeding.channel,
						e->proceeding.subcmds, e->proceeding.call);
					if (!pri->pvts[chanpos]->proceeding) {
						struct ast_frame f = { AST_FRAME_CONTROL, .subclass.integer = AST_CONTROL_PROCEEDING, };

						ast_debug(1, "Queuing frame from PRI_EVENT_PROCEEDING on channel %d/%d span %d\n",
							pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset,pri->span);
						pri_queue_frame(pri->pvts[chanpos], &f, pri);
						if (
#ifdef PRI_PROGRESS_MASK
							e->proceeding.progressmask & PRI_PROG_INBAND_AVAILABLE
#else
							e->proceeding.progress == 8
#endif
							) {
							/* Bring voice path up */
							f.subclass.integer = AST_CONTROL_PROGRESS;
							pri_queue_frame(pri->pvts[chanpos], &f, pri);
						}
						pri->pvts[chanpos]->proceeding = 1;
						sig_pri_set_dialing(pri->pvts[chanpos], 0);
					}
					sig_pri_unlock_private(pri->pvts[chanpos]);
				}
				break;
			case PRI_EVENT_FACILITY:
				chanpos = pri_find_principle(pri, e->facility.channel, e->facility.call);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Facility requested on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->facility.channel), PRI_CHANNEL(e->facility.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->facility.call);
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Facility requested on channel %d/%d not in use on span %d\n",
							PRI_SPAN(e->facility.channel), PRI_CHANNEL(e->facility.channel), pri->span);
					} else {
						sig_pri_lock_private(pri->pvts[chanpos]);
#if defined(HAVE_PRI_CALL_REROUTING)
						sig_pri_handle_subcmds(pri, chanpos, e->e, e->facility.channel,
							e->facility.subcmds, e->facility.subcall);
#else
						sig_pri_handle_subcmds(pri, chanpos, e->e, e->facility.channel,
							e->facility.subcmds, e->facility.call);
#endif	/* !defined(HAVE_PRI_CALL_REROUTING) */
						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				}
				break;
			case PRI_EVENT_ANSWER:
				chanpos = pri_find_principle(pri, e->answer.channel, e->answer.call);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Answer on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->answer.channel), PRI_CHANNEL(e->answer.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->answer.call);
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Answer requested on channel %d/%d not in use on span %d\n",
							PRI_SPAN(e->answer.channel), PRI_CHANNEL(e->answer.channel), pri->span);
					} else {
						sig_pri_lock_private(pri->pvts[chanpos]);

						sig_pri_handle_subcmds(pri, chanpos, e->e, e->answer.channel,
							e->answer.subcmds, e->answer.call);
						pri_queue_control(pri->pvts[chanpos], AST_CONTROL_ANSWER, pri);
						/* Enable echo cancellation if it's not on already */
						sig_pri_set_dialing(pri->pvts[chanpos], 0);
						sig_pri_set_echocanceller(pri->pvts[chanpos], 1);

#ifdef SUPPORT_USERUSER
						if (!ast_strlen_zero(e->answer.useruserinfo)) {
							struct ast_channel *owner;

							sig_pri_lock_owner(pri, chanpos);
							owner = pri->pvts[chanpos]->owner;
							if (owner) {
								pbx_builtin_setvar_helper(owner, "USERUSERINFO",
									e->answer.useruserinfo);
								ast_channel_unlock(owner);
							}
						}
#endif

						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				}
				break;
			case PRI_EVENT_HANGUP:
				chanpos = pri_find_principle(pri, e->hangup.channel, e->hangup.call);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Hangup requested on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->hangup.call);
					if (chanpos > -1) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						sig_pri_handle_subcmds(pri, chanpos, e->e, e->hangup.channel,
							e->hangup.subcmds, e->hangup.call);
						if (!pri->pvts[chanpos]->alreadyhungup) {
							/* we're calling here dahdi_hangup so once we get there we need to clear p->call after calling pri_hangup */
							pri->pvts[chanpos]->alreadyhungup = 1;
							if (pri->pvts[chanpos]->owner) {
								/* Queue a BUSY instead of a hangup if our cause is appropriate */
								pri->pvts[chanpos]->owner->hangupcause = e->hangup.cause;
								switch (pri->pvts[chanpos]->owner->_state) {
								case AST_STATE_BUSY:
								case AST_STATE_UP:
									ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
									break;
								default:
									switch (e->hangup.cause) {
									case PRI_CAUSE_USER_BUSY:
										pri_queue_control(pri->pvts[chanpos], AST_CONTROL_BUSY, pri);
										break;
									case PRI_CAUSE_CALL_REJECTED:
									case PRI_CAUSE_NETWORK_OUT_OF_ORDER:
									case PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION:
									case PRI_CAUSE_SWITCH_CONGESTION:
									case PRI_CAUSE_DESTINATION_OUT_OF_ORDER:
									case PRI_CAUSE_NORMAL_TEMPORARY_FAILURE:
										pri_queue_control(pri->pvts[chanpos], AST_CONTROL_CONGESTION, pri);
										break;
									default:
										ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
										break;
									}
									break;
								}
							}
							ast_verb(3, "Channel %d/%d, span %d got hangup, cause %d\n",
								pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span, e->hangup.cause);
						} else {
							pri_hangup(pri->pri, pri->pvts[chanpos]->call, e->hangup.cause);
							pri->pvts[chanpos]->call = NULL;
						}
						if (e->hangup.cause == PRI_CAUSE_REQUESTED_CHAN_UNAVAIL) {
							ast_verb(3, "Forcing restart of channel %d/%d on span %d since channel reported in use\n",
								PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
							pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[chanpos]));
							pri->pvts[chanpos]->resetting = 1;
						}
						if (e->hangup.aoc_units > -1)
							ast_verb(3, "Channel %d/%d, span %d received AOC-E charging %d unit%s\n",
								pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span, (int)e->hangup.aoc_units, (e->hangup.aoc_units == 1) ? "" : "s");

#ifdef SUPPORT_USERUSER
						if (!ast_strlen_zero(e->hangup.useruserinfo)) {
							struct ast_channel *owner;

							sig_pri_lock_owner(pri, chanpos);
							owner = pri->pvts[chanpos]->owner;
							if (owner) {
								pbx_builtin_setvar_helper(owner, "USERUSERINFO",
									e->hangup.useruserinfo);
								ast_channel_unlock(owner);
							}
						}
#endif

						sig_pri_unlock_private(pri->pvts[chanpos]);
					} else {
						ast_log(LOG_WARNING, "Hangup on bad channel %d/%d on span %d\n",
							PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					}
				}
				break;
			case PRI_EVENT_HANGUP_REQ:
				chanpos = pri_find_principle(pri, e->hangup.channel, e->hangup.call);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Hangup REQ requested on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->hangup.call);
					if (chanpos > -1) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						sig_pri_handle_subcmds(pri, chanpos, e->e, e->hangup.channel,
							e->hangup.subcmds, e->hangup.call);
#if defined(HAVE_PRI_CALL_HOLD)
						if (e->hangup.call_active && e->hangup.call_held
							&& pri->hold_disconnect_transfer) {
							/* We are to transfer the call instead of simply hanging up. */
							sig_pri_unlock_private(pri->pvts[chanpos]);
							if (!sig_pri_attempt_transfer(pri, e->hangup.call_active,
								e->hangup.call_held)) {
								break;
							}
							sig_pri_lock_private(pri->pvts[chanpos]);
						}
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
						if (pri->pvts[chanpos]->owner) {
							pri->pvts[chanpos]->owner->hangupcause = e->hangup.cause;
							switch (pri->pvts[chanpos]->owner->_state) {
							case AST_STATE_BUSY:
							case AST_STATE_UP:
								ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
								break;
							default:
								switch (e->hangup.cause) {
								case PRI_CAUSE_USER_BUSY:
									pri_queue_control(pri->pvts[chanpos], AST_CONTROL_BUSY, pri);
									break;
								case PRI_CAUSE_CALL_REJECTED:
								case PRI_CAUSE_NETWORK_OUT_OF_ORDER:
								case PRI_CAUSE_NORMAL_CIRCUIT_CONGESTION:
								case PRI_CAUSE_SWITCH_CONGESTION:
								case PRI_CAUSE_DESTINATION_OUT_OF_ORDER:
								case PRI_CAUSE_NORMAL_TEMPORARY_FAILURE:
									pri_queue_control(pri->pvts[chanpos], AST_CONTROL_CONGESTION, pri);
									break;
								default:
									ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
									break;
								}
								break;
							}
							ast_verb(3, "Channel %d/%d, span %d got hangup request, cause %d\n", PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span, e->hangup.cause);
							if (e->hangup.aoc_units > -1)
								ast_verb(3, "Channel %d/%d, span %d received AOC-E charging %d unit%s\n",
									pri->pvts[chanpos]->logicalspan, pri->pvts[chanpos]->prioffset, pri->span, (int)e->hangup.aoc_units, (e->hangup.aoc_units == 1) ? "" : "s");
						} else {
							pri_hangup(pri->pri, pri->pvts[chanpos]->call, e->hangup.cause);
							pri->pvts[chanpos]->call = NULL;
						}
						if (e->hangup.cause == PRI_CAUSE_REQUESTED_CHAN_UNAVAIL) {
							ast_verb(3, "Forcing restart of channel %d/%d span %d since channel reported in use\n",
								PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
							pri_reset(pri->pri, PVT_TO_CHANNEL(pri->pvts[chanpos]));
							pri->pvts[chanpos]->resetting = 1;
						}

#ifdef SUPPORT_USERUSER
						if (!ast_strlen_zero(e->hangup.useruserinfo)) {
							struct ast_channel *owner;

							sig_pri_lock_owner(pri, chanpos);
							owner = pri->pvts[chanpos]->owner;
							if (owner) {
								pbx_builtin_setvar_helper(owner, "USERUSERINFO",
									e->hangup.useruserinfo);
								ast_channel_unlock(owner);
							}
						}
#endif

						sig_pri_unlock_private(pri->pvts[chanpos]);
					} else {
						ast_log(LOG_WARNING, "Hangup REQ on bad channel %d/%d on span %d\n", PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
					}
				}
				break;
			case PRI_EVENT_HANGUP_ACK:
				chanpos = pri_find_principle(pri, e->hangup.channel, e->hangup.call);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Hangup ACK requested on unconfigured channel number %d/%d span %d\n",
						PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->hangup.call);
					if (chanpos > -1) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						pri->pvts[chanpos]->call = NULL;
						pri->pvts[chanpos]->resetting = 0;
						if (pri->pvts[chanpos]->owner) {
							ast_verb(3, "Channel %d/%d, span %d got hangup ACK\n", PRI_SPAN(e->hangup.channel), PRI_CHANNEL(e->hangup.channel), pri->span);
						}

#ifdef SUPPORT_USERUSER
						if (!ast_strlen_zero(e->hangup.useruserinfo)) {
							struct ast_channel *owner;

							sig_pri_lock_owner(pri, chanpos);
							owner = pri->pvts[chanpos]->owner;
							if (owner) {
								pbx_builtin_setvar_helper(owner, "USERUSERINFO",
									e->hangup.useruserinfo);
								ast_channel_unlock(owner);
							}
						}
#endif

						sig_pri_unlock_private(pri->pvts[chanpos]);
					}
				}
				break;
			case PRI_EVENT_CONFIG_ERR:
				ast_log(LOG_WARNING, "PRI Error on span %d: %s\n", pri->trunkgroup, e->err.err);
				break;
			case PRI_EVENT_RESTART_ACK:
				chanpos = pri_find_principle(pri, e->restartack.channel, NULL);
				if (chanpos < 0) {
					/* Sometime switches (e.g. I421 / British Telecom) don't give us the
					   channel number, so we have to figure it out...  This must be why
					   everybody resets exactly a channel at a time. */
					for (x = 0; x < pri->numchans; x++) {
						if (pri->pvts[x] && pri->pvts[x]->resetting) {
							chanpos = x;
							sig_pri_lock_private(pri->pvts[chanpos]);
							ast_debug(1, "Assuming restart ack is really for channel %d/%d span %d\n", pri->pvts[chanpos]->logicalspan,
								pri->pvts[chanpos]->prioffset, pri->span);
							if (pri->pvts[chanpos]->owner) {
								ast_log(LOG_WARNING, "Got restart ack on channel %d/%d with owner on span %d\n", pri->pvts[chanpos]->logicalspan,
									pri->pvts[chanpos]->prioffset, pri->span);
								ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
							}
							pri->pvts[chanpos]->resetting = 0;
							ast_verb(3, "B-channel %d/%d successfully restarted on span %d\n", pri->pvts[chanpos]->logicalspan,
								pri->pvts[chanpos]->prioffset, pri->span);
							sig_pri_unlock_private(pri->pvts[chanpos]);
							if (pri->resetting)
								pri_check_restart(pri);
							break;
						}
					}
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Restart ACK requested on strange channel %d/%d span %d\n",
							PRI_SPAN(e->restartack.channel), PRI_CHANNEL(e->restartack.channel), pri->span);
					}
				} else {
					if (pri->pvts[chanpos]) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						if (pri->pvts[chanpos]->owner) {
							ast_log(LOG_WARNING, "Got restart ack on channel %d/%d span %d with owner\n",
								PRI_SPAN(e->restartack.channel), PRI_CHANNEL(e->restartack.channel), pri->span);
							ast_softhangup_nolock(pri->pvts[chanpos]->owner, AST_SOFTHANGUP_DEV);
						}
						pri->pvts[chanpos]->resetting = 0;
						ast_verb(3, "B-channel %d/%d successfully restarted on span %d\n", pri->pvts[chanpos]->logicalspan,
							pri->pvts[chanpos]->prioffset, pri->span);
						sig_pri_unlock_private(pri->pvts[chanpos]);
						if (pri->resetting)
							pri_check_restart(pri);
					}
				}
				break;
			case PRI_EVENT_SETUP_ACK:
				chanpos = pri_find_principle(pri, e->setup_ack.channel, e->setup_ack.call);
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received SETUP_ACKNOWLEDGE on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->setup_ack.channel), PRI_CHANNEL(e->setup_ack.channel), pri->span);
				} else {
					chanpos = pri_fixup_principle(pri, chanpos, e->setup_ack.call);
					if (chanpos > -1) {
						sig_pri_lock_private(pri->pvts[chanpos]);
						sig_pri_handle_subcmds(pri, chanpos, e->e, e->setup_ack.channel,
							e->setup_ack.subcmds, e->setup_ack.call);
						pri->pvts[chanpos]->setup_ack = 1;
						/* Send any queued digits */
						for (x = 0;x < strlen(pri->pvts[chanpos]->dialdest); x++) {
							ast_debug(1, "Sending pending digit '%c'\n", pri->pvts[chanpos]->dialdest[x]);
							pri_information(pri->pri, pri->pvts[chanpos]->call,
								pri->pvts[chanpos]->dialdest[x]);
						}
						sig_pri_unlock_private(pri->pvts[chanpos]);
					} else
						ast_log(LOG_WARNING, "Unable to move channel %d!\n", e->setup_ack.channel);
				}
				break;
			case PRI_EVENT_NOTIFY:
#if defined(HAVE_PRI_CALL_HOLD)
				chanpos = pri_find_principle(pri, e->notify.channel, e->notify.call);
#else
				chanpos = pri_find_principle(pri, e->notify.channel, NULL);
#endif	/* !defined(HAVE_PRI_CALL_HOLD) */
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received NOTIFY on unconfigured channel %d/%d span %d\n",
						PRI_SPAN(e->notify.channel), PRI_CHANNEL(e->notify.channel), pri->span);
				} else {
					sig_pri_lock_private(pri->pvts[chanpos]);
#if defined(HAVE_PRI_CALL_HOLD)
					sig_pri_handle_subcmds(pri, chanpos, e->e, e->notify.channel,
						e->notify.subcmds, e->notify.call);
#else
					sig_pri_handle_subcmds(pri, chanpos, e->e, e->notify.channel,
						e->notify.subcmds, NULL);
#endif	/* !defined(HAVE_PRI_CALL_HOLD) */
					switch (e->notify.info) {
					case PRI_NOTIFY_REMOTE_HOLD:
						if (!pri->discardremoteholdretrieval) {
							struct ast_frame f = { AST_FRAME_CONTROL, };

							f.subclass.integer = AST_CONTROL_HOLD;
							pri_queue_frame(pri->pvts[chanpos], &f, pri);
						}
						break;
					case PRI_NOTIFY_REMOTE_RETRIEVAL:
						if (!pri->discardremoteholdretrieval) {
							struct ast_frame f = { AST_FRAME_CONTROL, };

							f.subclass.integer = AST_CONTROL_UNHOLD;
							pri_queue_frame(pri->pvts[chanpos], &f, pri);
						}
						break;
					}
					sig_pri_unlock_private(pri->pvts[chanpos]);
				}
				break;
#if defined(HAVE_PRI_CALL_HOLD)
			case PRI_EVENT_HOLD:
				if (sig_pri_handle_hold(pri, e)) {
					pri_hold_rej(pri->pri, e->hold.call,
						PRI_CAUSE_RESOURCE_UNAVAIL_UNSPECIFIED);
				} else {
					pri_hold_ack(pri->pri, e->hold.call);
				}
				break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
#if defined(HAVE_PRI_CALL_HOLD)
			case PRI_EVENT_HOLD_ACK:
				ast_debug(1, "Event: HOLD_ACK\n");
				break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
#if defined(HAVE_PRI_CALL_HOLD)
			case PRI_EVENT_HOLD_REJ:
				ast_debug(1, "Event: HOLD_REJ\n");
				break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
#if defined(HAVE_PRI_CALL_HOLD)
			case PRI_EVENT_RETRIEVE:
				sig_pri_handle_retrieve(pri, e);
				break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
#if defined(HAVE_PRI_CALL_HOLD)
			case PRI_EVENT_RETRIEVE_ACK:
				ast_debug(1, "Event: RETRIEVE_ACK\n");
				break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
#if defined(HAVE_PRI_CALL_HOLD)
			case PRI_EVENT_RETRIEVE_REJ:
				ast_debug(1, "Event: RETRIEVE_REJ\n");
				break;
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
			default:
				ast_debug(1, "Event: %d\n", e->e);
				break;
			}
		}
		ast_mutex_unlock(&pri->lock);
	}
	/* Never reached */
	return NULL;
}

void sig_pri_init_pri(struct sig_pri_pri *pri)
{
	int i;

	memset(pri, 0, sizeof(*pri));

	ast_mutex_init(&pri->lock);

	pri->master = AST_PTHREADT_NULL;
	for (i = 0; i < NUM_DCHANS; i++)
		pri->fds[i] = -1;
}

int sig_pri_hangup(struct sig_pri_chan *p, struct ast_channel *ast)
{
	int res = 0;
#ifdef SUPPORT_USERUSER
	const char *useruser = pbx_builtin_getvar_helper(ast, "USERUSERINFO");
#endif

	ast_log(LOG_DEBUG, "%s %d\n", __FUNCTION__, p->channel);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}

	p->owner = NULL;
	p->outgoing = 0;
	sig_pri_set_digital(p, 0);	/* push up to parent for EC*/
	p->proceeding = 0;
	p->progress = 0;
	p->alerting = 0;
	p->setup_ack = 0;
	p->cid_num[0] = '\0';
	p->cid_subaddr[0] = '\0';
	p->cid_name[0] = '\0';
	p->exten[0] = '\0';
	sig_pri_set_dialing(p, 0);

	if (!p->call) {
		res = 0;
		goto exit;
	}

	/* Make sure we have a call (or REALLY have a call in the case of a PRI) */
	if (!pri_grab(p, p->pri)) {
		if (p->alreadyhungup) {
			ast_log(LOG_DEBUG, "Already hungup...  Calling hangup once, and clearing call\n");

#ifdef SUPPORT_USERUSER
			pri_call_set_useruser(p->call, useruser);
#endif

			pri_hangup(p->pri->pri, p->call, -1);
			p->call = NULL;
		} else {
			const char *cause = pbx_builtin_getvar_helper(ast,"PRI_CAUSE");
			int icause = ast->hangupcause ? ast->hangupcause : -1;
			ast_log(LOG_DEBUG, "Not yet hungup...  Calling hangup once with icause, and clearing call\n");

#ifdef SUPPORT_USERUSER
			pri_call_set_useruser(p->call, useruser);
#endif

			p->alreadyhungup = 1;
			if (cause) {
				if (atoi(cause))
					icause = atoi(cause);
			}
			pri_hangup(p->pri->pri, p->call, icause);
		}
		if (res < 0)
			ast_log(LOG_WARNING, "pri_disconnect failed\n");
		pri_rel(p->pri);
	} else {
		ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
		res = -1;
	}

exit:
	ast->tech_pvt = NULL;
	return res;
}

/*!
 * \brief Extract the called number and subaddress from the dial string.
 * \since 1.8
 *
 * \param p sig_pri channel structure.
 * \param rdest Dial string buffer to extract called number and subaddress.
 * \param called Buffer to fill with extracted <number>[:<subaddress>]
 * \param called_buff_size Size of buffer to fill.
 *
 * \note Parsing must remain in sync with sig_pri_call().
 *
 * \return Nothing
 */
void sig_pri_extract_called_num_subaddr(struct sig_pri_chan *p, const char *rdest, char *called, size_t called_buff_size)
{
	char *dial;
	char *number;
	char *subaddr;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(group);	/* channel/group token */
		AST_APP_ARG(ext);	/* extension token */
		//AST_APP_ARG(opts);	/* options token */
		AST_APP_ARG(other);	/* Any remining unused arguments */
	);

	/* Get private copy of dial string and break it up. */
	dial = ast_strdupa(rdest);
	AST_NONSTANDARD_APP_ARGS(args, dial, '/');

	number = args.ext;
	if (!number) {
		number = "";
	}

	/* Find and extract dialed_subaddress */
	subaddr = strchr(number, ':');
	if (subaddr) {
		*subaddr++ = '\0';

		/* Skip subaddress type prefix. */
		switch (*subaddr) {
		case 'U':
		case 'u':
		case 'N':
		case 'n':
			++subaddr;
			break;
		default:
			break;
		}
	}

	/* Skip type-of-number/dial-plan prefix characters. */
	if (strlen(number) < p->stripmsd) {
		number = "";
	} else {
		number += p->stripmsd;
		while (isalpha(*number)) {
			++number;
		}
	}

	/* Fill buffer with extracted number and subaddress. */
	if (ast_strlen_zero(subaddr)) {
		/* Put in called number only since there is no subaddress. */
		snprintf(called, called_buff_size, "%s", number);
	} else {
		/* Put in called number and subaddress. */
		snprintf(called, called_buff_size, "%s:%s", number, subaddr);
	}
}

enum SIG_PRI_CALL_OPT_FLAGS {
	OPT_KEYPAD =         (1 << 0),
	OPT_REVERSE_CHARGE = (1 << 1),	/* Collect call */
};
enum SIG_PRI_CALL_OPT_ARGS {
	OPT_ARG_KEYPAD = 0,

	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(sig_pri_call_opts, BEGIN_OPTIONS
	AST_APP_OPTION_ARG('K', OPT_KEYPAD, OPT_ARG_KEYPAD),
	AST_APP_OPTION('R', OPT_REVERSE_CHARGE),
END_OPTIONS);

/*! \note Parsing must remain in sync with sig_pri_extract_called_num_subaddr(). */
int sig_pri_call(struct sig_pri_chan *p, struct ast_channel *ast, char *rdest, int timeout, int layer1)
{
	char dest[256]; /* must be same length as p->dialdest */
	struct ast_party_subaddress dialed_subaddress; /* Called subaddress */
	struct pri_sr *sr;
	char *c, *l, *n, *s;
#ifdef SUPPORT_USERUSER
	const char *useruser;
#endif
	int pridialplan;
	int dp_strip;
	int prilocaldialplan;
	int ldp_strip;
	int exclusive;
#if defined(HAVE_PRI_SETUP_KEYPAD)
	const char *keypad;
#endif	/* defined(HAVE_PRI_SETUP_KEYPAD) */
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(group);	/* channel/group token */
		AST_APP_ARG(ext);	/* extension token */
		AST_APP_ARG(opts);	/* options token */
		AST_APP_ARG(other);	/* Any remining unused arguments */
	);
	struct ast_flags opts;
	char *opt_args[OPT_ARG_ARRAY_SIZE];

	ast_log(LOG_DEBUG, "CALLING CID_NAME: %s CID_NUM:: %s\n", ast->cid.cid_name, ast->cid.cid_num);

	if (!p->pri) {
		ast_log(LOG_ERROR, "Could not find pri on channel %d\n", p->channel);
		return -1;
	}

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "sig_pri_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}

	p->dialdest[0] = '\0';
	p->outgoing = 1;

	ast_copy_string(dest, rdest, sizeof(dest));
	AST_NONSTANDARD_APP_ARGS(args, dest, '/');
	if (ast_app_parse_options(sig_pri_call_opts, &opts, opt_args, args.opts)) {
		/* General invalid option syntax. */
		return -1;
	}

	c = args.ext;
	if (!c) {
		c = "";
	}

	/* setup dialed_subaddress if found */
	ast_party_subaddress_init(&dialed_subaddress);
	s = strchr(c, ':');
	if (s) {
		*s = '\0';
		s++;
		/* prefix */
		/* 'n' = NSAP */
		/* 'U' = odd, 'u'= even */
		/* Default = NSAP */
		switch (*s) {
		case 'U':
			dialed_subaddress.odd_even_indicator = 1;
			/* fall through */
		case 'u':
			s++;
			dialed_subaddress.type = 2;
			break;
		case 'N':
		case 'n':
			s++;
			/* default already covered with ast_party_subaddress_init */
			break;
		}
		dialed_subaddress.str = s;
		dialed_subaddress.valid = 1;
		s = NULL;
	}

	l = NULL;
	n = NULL;
	if (!p->hidecallerid) {
		l = ast->connected.id.number;
		if (!p->hidecalleridname) {
			n = ast->connected.id.name;
		}
	}

	if (strlen(c) < p->stripmsd) {
		ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
		return -1;
	}
	if (pri_grab(p, p->pri)) {
		ast_log(LOG_WARNING, "Failed to grab PRI!\n");
		return -1;
	}
	if (!(p->call = pri_new_call(p->pri->pri))) {
		ast_log(LOG_WARNING, "Unable to create call on channel %d\n", p->channel);
		pri_rel(p->pri);
		return -1;
	}
	if (!(sr = pri_sr_new())) {
		ast_log(LOG_WARNING, "Failed to allocate setup request channel %d\n", p->channel);
		pri_destroycall(p->pri->pri, p->call);
		p->call = NULL;
		pri_rel(p->pri);
		return -1;
	}

	sig_pri_set_digital(p, IS_DIGITAL(ast->transfercapability));	/* push up to parent for EC */

	/* Should the picked channel be used exclusively? */
	if (p->priexclusive || p->pri->nodetype == PRI_NETWORK) {
		exclusive = 1;
	} else {
		exclusive = 0;
	}

	pri_sr_set_channel(sr, PVT_TO_CHANNEL(p), exclusive, 1);
	pri_sr_set_bearer(sr, p->digital ? PRI_TRANS_CAP_DIGITAL : ast->transfercapability,
		(p->digital ? -1 : layer1));

	if (p->pri->facilityenable)
		pri_facility_enable(p->pri->pri);

	ast_verb(3, "Requested transfer capability: 0x%.2x - %s\n", ast->transfercapability, ast_transfercapability2str(ast->transfercapability));
	dp_strip = 0;
	pridialplan = p->pri->dialplan - 1;
	if (pridialplan == -2 || pridialplan == -3) { /* compute dynamically */
		if (strncmp(c + p->stripmsd, p->pri->internationalprefix, strlen(p->pri->internationalprefix)) == 0) {
			if (pridialplan == -2) {
				dp_strip = strlen(p->pri->internationalprefix);
			}
			pridialplan = PRI_INTERNATIONAL_ISDN;
		} else if (strncmp(c + p->stripmsd, p->pri->nationalprefix, strlen(p->pri->nationalprefix)) == 0) {
			if (pridialplan == -2) {
				dp_strip = strlen(p->pri->nationalprefix);
			}
			pridialplan = PRI_NATIONAL_ISDN;
		} else {
			pridialplan = PRI_LOCAL_ISDN;
		}
	}
	while (c[p->stripmsd] > '9' && c[p->stripmsd] != '*' && c[p->stripmsd] != '#') {
		switch (c[p->stripmsd]) {
		case 'U':
			pridialplan = (PRI_TON_UNKNOWN << 4) | (pridialplan & 0xf);
			break;
		case 'I':
			pridialplan = (PRI_TON_INTERNATIONAL << 4) | (pridialplan & 0xf);
			break;
		case 'N':
			pridialplan = (PRI_TON_NATIONAL << 4) | (pridialplan & 0xf);
			break;
		case 'L':
			pridialplan = (PRI_TON_NET_SPECIFIC << 4) | (pridialplan & 0xf);
			break;
		case 'S':
			pridialplan = (PRI_TON_SUBSCRIBER << 4) | (pridialplan & 0xf);
			break;
		case 'V':
			pridialplan = (PRI_TON_ABBREVIATED << 4) | (pridialplan & 0xf);
			break;
		case 'R':
			pridialplan = (PRI_TON_RESERVED << 4) | (pridialplan & 0xf);
			break;
		case 'u':
			pridialplan = PRI_NPI_UNKNOWN | (pridialplan & 0xf0);
			break;
		case 'e':
			pridialplan = PRI_NPI_E163_E164 | (pridialplan & 0xf0);
			break;
		case 'x':
			pridialplan = PRI_NPI_X121 | (pridialplan & 0xf0);
			break;
		case 'f':
			pridialplan = PRI_NPI_F69 | (pridialplan & 0xf0);
			break;
		case 'n':
			pridialplan = PRI_NPI_NATIONAL | (pridialplan & 0xf0);
			break;
		case 'p':
			pridialplan = PRI_NPI_PRIVATE | (pridialplan & 0xf0);
			break;
		case 'r':
			pridialplan = PRI_NPI_RESERVED | (pridialplan & 0xf0);
			break;
		default:
			if (isalpha(c[p->stripmsd])) {
				ast_log(LOG_WARNING, "Unrecognized pridialplan %s modifier: %c\n",
					c[p->stripmsd] > 'Z' ? "NPI" : "TON", c[p->stripmsd]);
			}
			break;
		}
		c++;
	}
#if defined(HAVE_PRI_SETUP_KEYPAD)
	if (ast_test_flag(&opts, OPT_KEYPAD)
		&& !ast_strlen_zero(opt_args[OPT_ARG_KEYPAD])) {
		/* We have a keypad facility digits option with digits. */
		keypad = opt_args[OPT_ARG_KEYPAD];
		pri_sr_set_keypad_digits(sr, keypad);
	} else {
		keypad = NULL;
	}
	if (!keypad || !ast_strlen_zero(c + p->stripmsd + dp_strip))
#endif	/* defined(HAVE_PRI_SETUP_KEYPAD) */
	{
		pri_sr_set_called(sr, c + p->stripmsd + dp_strip, pridialplan, s ? 1 : 0);
	}

#if defined(HAVE_PRI_SUBADDR)
	if (dialed_subaddress.valid) {
		struct pri_party_subaddress subaddress;

		memset(&subaddress, 0, sizeof(subaddress));
		sig_pri_party_subaddress_from_ast(&subaddress, &dialed_subaddress);
		pri_sr_set_called_subaddress(sr, &subaddress);
	}
#endif	/* defined(HAVE_PRI_SUBADDR) */

#if defined(HAVE_PRI_REVERSE_CHARGE)
	if (ast_test_flag(&opts, OPT_REVERSE_CHARGE)) {
		pri_sr_set_reversecharge(sr, PRI_REVERSECHARGE_REQUESTED);
	}
#endif	/* defined(HAVE_PRI_REVERSE_CHARGE) */

	ldp_strip = 0;
	prilocaldialplan = p->pri->localdialplan - 1;
	if ((l != NULL) && (prilocaldialplan == -2 || prilocaldialplan == -3)) { /* compute dynamically */
		if (strncmp(l, p->pri->internationalprefix, strlen(p->pri->internationalprefix)) == 0) {
			if (prilocaldialplan == -2) {
				ldp_strip = strlen(p->pri->internationalprefix);
			}
			prilocaldialplan = PRI_INTERNATIONAL_ISDN;
		} else if (strncmp(l, p->pri->nationalprefix, strlen(p->pri->nationalprefix)) == 0) {
			if (prilocaldialplan == -2) {
				ldp_strip = strlen(p->pri->nationalprefix);
			}
			prilocaldialplan = PRI_NATIONAL_ISDN;
		} else {
			prilocaldialplan = PRI_LOCAL_ISDN;
		}
	}
	if (l != NULL) {
		while (*l > '9' && *l != '*' && *l != '#') {
			switch (*l) {
			case 'U':
				prilocaldialplan = (PRI_TON_UNKNOWN << 4) | (prilocaldialplan & 0xf);
				break;
			case 'I':
				prilocaldialplan = (PRI_TON_INTERNATIONAL << 4) | (prilocaldialplan & 0xf);
				break;
			case 'N':
				prilocaldialplan = (PRI_TON_NATIONAL << 4) | (prilocaldialplan & 0xf);
				break;
			case 'L':
				prilocaldialplan = (PRI_TON_NET_SPECIFIC << 4) | (prilocaldialplan & 0xf);
				break;
			case 'S':
				prilocaldialplan = (PRI_TON_SUBSCRIBER << 4) | (prilocaldialplan & 0xf);
				break;
			case 'V':
				prilocaldialplan = (PRI_TON_ABBREVIATED << 4) | (prilocaldialplan & 0xf);
				break;
			case 'R':
				prilocaldialplan = (PRI_TON_RESERVED << 4) | (prilocaldialplan & 0xf);
				break;
			case 'u':
				prilocaldialplan = PRI_NPI_UNKNOWN | (prilocaldialplan & 0xf0);
				break;
			case 'e':
				prilocaldialplan = PRI_NPI_E163_E164 | (prilocaldialplan & 0xf0);
				break;
			case 'x':
				prilocaldialplan = PRI_NPI_X121 | (prilocaldialplan & 0xf0);
				break;
			case 'f':
				prilocaldialplan = PRI_NPI_F69 | (prilocaldialplan & 0xf0);
				break;
			case 'n':
				prilocaldialplan = PRI_NPI_NATIONAL | (prilocaldialplan & 0xf0);
				break;
			case 'p':
				prilocaldialplan = PRI_NPI_PRIVATE | (prilocaldialplan & 0xf0);
				break;
			case 'r':
				prilocaldialplan = PRI_NPI_RESERVED | (prilocaldialplan & 0xf0);
				break;
			default:
				if (isalpha(*l)) {
					ast_log(LOG_WARNING,
						"Unrecognized prilocaldialplan %s modifier: %c\n",
						*l > 'Z' ? "NPI" : "TON", *l);
				}
				break;
			}
			l++;
		}
	}
	pri_sr_set_caller(sr, l ? (l + ldp_strip) : NULL, n, prilocaldialplan,
		p->use_callingpres ? ast->connected.id.number_presentation : (l ? PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN : PRES_NUMBER_NOT_AVAILABLE));

#if defined(HAVE_PRI_SUBADDR)
	if (ast->connected.id.subaddress.valid) {
		struct pri_party_subaddress subaddress;

		memset(&subaddress, 0, sizeof(subaddress));
		sig_pri_party_subaddress_from_ast(&subaddress, &ast->connected.id.subaddress);
		pri_sr_set_caller_subaddress(sr, &subaddress);
	}
#endif	/* defined(HAVE_PRI_SUBADDR) */

	sig_pri_redirecting_update(p, ast);

#ifdef SUPPORT_USERUSER
	/* User-user info */
	useruser = pbx_builtin_getvar_helper(p->owner, "USERUSERINFO");
	if (useruser)
		pri_sr_set_useruser(sr, useruser);
#endif

	if (pri_setup(p->pri->pri, p->call, sr)) {
		ast_log(LOG_WARNING, "Unable to setup call to %s (using %s)\n",
			c + p->stripmsd + dp_strip, dialplan2str(p->pri->dialplan));
		pri_rel(p->pri);
		pri_sr_free(sr);
		return -1;
	}
	pri_sr_free(sr);
	ast_setstate(ast, AST_STATE_DIALING);
	sig_pri_set_dialing(p, 1);
	pri_rel(p->pri);
	return 0;
}

int sig_pri_indicate(struct sig_pri_chan *p, struct ast_channel *chan, int condition, const void *data, size_t datalen)
{
	int res = -1;

	switch (condition) {
	case AST_CONTROL_BUSY:
		if (p->priindication_oob) {
			chan->hangupcause = AST_CAUSE_USER_BUSY;
			chan->_softhangup |= AST_SOFTHANGUP_DEV;
			res = 0;
		} else if (!p->progress && p->pri && !p->outgoing) {
			if (p->pri->pri) {
				if (!pri_grab(p, p->pri)) {
#ifdef HAVE_PRI_PROG_W_CAUSE
					pri_progress_with_cause(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1, PRI_CAUSE_USER_BUSY); /* cause = 17 */
#else
					pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
#endif
					pri_rel(p->pri);
				} else {
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
				}
			}
			p->progress = 1;
			res = sig_pri_play_tone(p, SIG_PRI_TONE_BUSY);
		}
		break;
	case AST_CONTROL_RINGING:
		if ((!p->alerting) && p->pri && !p->outgoing && (chan->_state != AST_STATE_UP)) {
			if (p->pri->pri) {
				if (!pri_grab(p, p->pri)) {
					pri_acknowledge(p->pri->pri,p->call, PVT_TO_CHANNEL(p), !p->digital);
					pri_rel(p->pri);
				} else {
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
				}
			}
			p->alerting = 1;
		}
		res = sig_pri_play_tone(p, SIG_PRI_TONE_RINGTONE);
		if (chan->_state != AST_STATE_UP) {
			if (chan->_state != AST_STATE_RING)
				ast_setstate(chan, AST_STATE_RINGING);
		}
		break;
	case AST_CONTROL_PROCEEDING:
		ast_debug(1,"Received AST_CONTROL_PROCEEDING on %s\n",chan->name);
		if (!p->proceeding && p->pri && !p->outgoing) {
			if (p->pri->pri) {
				if (!pri_grab(p, p->pri)) {
					pri_proceeding(p->pri->pri,p->call, PVT_TO_CHANNEL(p), !p->digital);
					pri_rel(p->pri);
				} else {
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
				}
			}
			p->proceeding = 1;
			sig_pri_set_dialing(p, 0);
		}
		/* don't continue in ast_indicate */
		res = 0;
		break;
	case AST_CONTROL_PROGRESS:
		ast_debug(1,"Received AST_CONTROL_PROGRESS on %s\n",chan->name);
		sig_pri_set_digital(p, 0);	/* Digital-only calls isn't allowing any inband progress messages */
		if (!p->progress && p->pri && !p->outgoing) {
			if (p->pri->pri) {
				if (!pri_grab(p, p->pri)) {
#ifdef HAVE_PRI_PROG_W_CAUSE
					pri_progress_with_cause(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1, -1);  /* no cause at all */
#else
					pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
#endif
					pri_rel(p->pri);
				} else {
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
				}
			}
			p->progress = 1;
		}
		/* don't continue in ast_indicate */
		res = 0;
		break;
	case AST_CONTROL_CONGESTION:
		chan->hangupcause = AST_CAUSE_CONGESTION;
		if (p->priindication_oob) {
			chan->hangupcause = AST_CAUSE_SWITCH_CONGESTION;
			chan->_softhangup |= AST_SOFTHANGUP_DEV;
			res = 0;
		} else if (!p->progress && p->pri && !p->outgoing) {
			if (p->pri->pri) {
				if (!pri_grab(p, p->pri)) {
#ifdef HAVE_PRI_PROG_W_CAUSE
					pri_progress_with_cause(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1, PRI_CAUSE_SWITCH_CONGESTION); /* cause = 42 */
#else
					pri_progress(p->pri->pri,p->call, PVT_TO_CHANNEL(p), 1);
#endif
					pri_rel(p->pri);
				} else {
					ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
				}
			}
			p->progress = 1;
			res = sig_pri_play_tone(p, SIG_PRI_TONE_CONGESTION);
		}
		break;
	case AST_CONTROL_HOLD:
		if (p->pri && !strcasecmp(p->mohinterpret, "passthrough")) {
			if (!pri_grab(p, p->pri)) {
				res = pri_notify(p->pri->pri, p->call, p->prioffset, PRI_NOTIFY_REMOTE_HOLD);
				pri_rel(p->pri);
			} else {
				ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", p->pri->span);
			}
		} else
			ast_moh_start(chan, data, p->mohinterpret);
		break;
	case AST_CONTROL_UNHOLD:
		if (p->pri && !strcasecmp(p->mohinterpret, "passthrough")) {
			if (!pri_grab(p, p->pri)) {
				res = pri_notify(p->pri->pri, p->call, p->prioffset, PRI_NOTIFY_REMOTE_RETRIEVAL);
				pri_rel(p->pri);
			}
		} else
			ast_moh_stop(chan);
		break;
	case AST_CONTROL_SRCUPDATE:
		res = 0;
		break;
	case -1:
		res = sig_pri_play_tone(p, -1);
		break;
	case AST_CONTROL_CONNECTED_LINE:
		ast_debug(1, "Received AST_CONTROL_CONNECTED_LINE on %s\n", chan->name);
		if (p->pri && !pri_grab(p, p->pri)) {
			struct pri_party_connected_line connected;

			memset(&connected, 0, sizeof(connected));
			sig_pri_party_id_from_ast(&connected.id, &chan->connected.id);

			pri_connected_line_update(p->pri->pri, p->call, &connected);
			pri_rel(p->pri);
		}
		break;
	case AST_CONTROL_REDIRECTING:
		ast_debug(1, "Received AST_CONTROL_REDIRECTING on %s\n", chan->name);
		if (p->pri && !pri_grab(p, p->pri)) {
			sig_pri_redirecting_update(p, chan);
			pri_rel(p->pri);
		}
		break;
	}

	return res;
}

int sig_pri_answer(struct sig_pri_chan *p, struct ast_channel *ast)
{
	int res = 0;
	/* Send a pri acknowledge */
	if (!pri_grab(p, p->pri)) {
		p->proceeding = 1;
		sig_pri_set_dialing(p, 0);
		res = pri_answer(p->pri->pri, p->call, 0, !p->digital);
		pri_rel(p->pri);
	} else {
		res = -1;
	}
	ast_setstate(ast, AST_STATE_UP);
	return res;
}

int sig_pri_available(struct sig_pri_chan *p, int *reason)
{
	/* If no owner and interface has a B channel then likely available */
	if (!p->owner && !p->no_b_channel && p->pri) {
#if defined(HAVE_PRI_SERVICE_MESSAGES)
		if (p->resetting || p->call || p->service_status) {
			if (p->service_status) {
				*reason = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
			}
			return 0;
		}
#else
		if (p->resetting || p->call) {
			return 0;
		}
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
		return 1;
	}

	return 0;
}

/* If return 0, it means this function was able to handle it (pre setup digits).  If non zero, the user of this
 * functions should handle it normally (generate inband DTMF) */
int sig_pri_digit_begin(struct sig_pri_chan *pvt, struct ast_channel *ast, char digit)
{
	if ((ast->_state == AST_STATE_DIALING) && !pvt->proceeding) {
		if (pvt->setup_ack) {
			if (!pri_grab(pvt, pvt->pri)) {
				pri_information(pvt->pri->pri, pvt->call, digit);
				pri_rel(pvt->pri);
			} else {
				ast_log(LOG_WARNING, "Unable to grab PRI on span %d\n", pvt->pri->span);
			}
		} else if (strlen(pvt->dialdest) < sizeof(pvt->dialdest) - 1) {
			int res;
			ast_debug(1, "Queueing digit '%c' since setup_ack not yet received\n", digit);
			res = strlen(pvt->dialdest);
			pvt->dialdest[res++] = digit;
			pvt->dialdest[res] = '\0';
		}
		return 0;
	}
	return 1;
}

int sig_pri_start_pri(struct sig_pri_pri *pri)
{
	int x;
	int i;

	ast_mutex_init(&pri->lock);

	for (i = 0; i < NUM_DCHANS; i++) {
		if (pri->fds[i] == -1) {
			break;
		}

		switch (pri->sig) {
		case SIG_BRI:
			pri->dchans[i] = pri_new_bri(pri->fds[i], 1, pri->nodetype, pri->switchtype);
			break;
		case SIG_BRI_PTMP:
			pri->dchans[i] = pri_new_bri(pri->fds[i], 0, pri->nodetype, pri->switchtype);
			break;
		default:
			pri->dchans[i] = pri_new(pri->fds[i], pri->nodetype, pri->switchtype);
#if defined(HAVE_PRI_SERVICE_MESSAGES)
			if (pri->enable_service_message_support) {
				pri_set_service_message_support(pri->dchans[i], 1);
			}
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */
			break;
		}

		pri_set_overlapdial(pri->dchans[i], (pri->overlapdial & DAHDI_OVERLAPDIAL_OUTGOING) ? 1 : 0);
#ifdef HAVE_PRI_PROG_W_CAUSE
		pri_set_chan_mapping_logical(pri->dchans[i], pri->qsigchannelmapping == DAHDI_CHAN_MAPPING_LOGICAL);
#endif
#ifdef HAVE_PRI_INBANDDISCONNECT
		pri_set_inbanddisconnect(pri->dchans[i], pri->inbanddisconnect);
#endif
#if defined(HAVE_PRI_CALL_HOLD)
		pri_hold_enable(pri->dchans[i], 1);
#endif	/* defined(HAVE_PRI_CALL_HOLD) */
#if defined(HAVE_PRI_CALL_REROUTING)
		pri_reroute_enable(pri->dchans[i], 1);
#endif	/* defined(HAVE_PRI_CALL_REROUTING) */
		/* Enslave to master if appropriate */
		if (i)
			pri_enslave(pri->dchans[0], pri->dchans[i]);
		if (!pri->dchans[i]) {
			if (pri->fds[i] > 0)
				close(pri->fds[i]);
			pri->fds[i] = -1;
			ast_log(LOG_ERROR, "Unable to create PRI structure\n");
			return -1;
		}
		pri_set_debug(pri->dchans[i], DEFAULT_PRI_DEBUG);
		pri_set_nsf(pri->dchans[i], pri->nsf);
#ifdef PRI_GETSET_TIMERS
		for (x = 0; x < PRI_MAX_TIMERS; x++) {
			if (pri->pritimers[x] != 0)
				pri_set_timer(pri->dchans[i], x, pri->pritimers[x]);
		}
#endif
	}
	/* Assume primary is the one we use */
	pri->pri = pri->dchans[0];
	pri->resetpos = -1;
	if (ast_pthread_create_background(&pri->master, NULL, pri_dchannel, pri)) {
		for (i = 0; i < NUM_DCHANS; i++) {
			if (!pri->dchans[i])
				break;
			if (pri->fds[i] > 0)
				close(pri->fds[i]);
			pri->fds[i] = -1;
		}
		ast_log(LOG_ERROR, "Unable to spawn D-channel: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

void sig_pri_chan_alarm_notify(struct sig_pri_chan *p, int noalarm)
{
	if (!noalarm) {
		p->inalarm = 1;
		if (!p->pri || !p->pri->pri || (pri_get_timer(p->pri->pri, PRI_TIMER_T309) < 0)) {
			/* T309 is not enabled : hangup calls when alarm occurs */
			if (p->call) {
				if (p->pri && p->pri->pri) {
					if (!pri_grab(p, p->pri)) {
						pri_hangup(p->pri->pri, p->call, -1);
						pri_destroycall(p->pri->pri, p->call);
						p->call = NULL;
						pri_rel(p->pri);
					} else
						ast_log(LOG_WARNING, "Failed to grab PRI!\n");
				} else
					ast_log(LOG_WARNING, "The PRI Call has not been destroyed\n");
			}
			if (p->owner)
				ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_DEV);
		}
	} else {
		p->inalarm = 0;
	}
}

struct sig_pri_chan *sig_pri_chan_new(void *pvt_data, struct sig_pri_callback *callback, struct sig_pri_pri *pri, int logicalspan, int channo, int trunkgroup)
{
	struct sig_pri_chan *p;

	p = ast_calloc(1, sizeof(*p));
	if (!p)
		return p;

	p->logicalspan = logicalspan;
	p->prioffset = channo;
	p->mastertrunkgroup = trunkgroup;

	p->calls = callback;
	p->chan_pvt = pvt_data;

	p->pri = pri;

	return p;
}

/*!
 * \brief Delete the sig_pri private channel structure.
 * \since 1.8
 *
 * \param doomed sig_pri private channel structure to delete.
 *
 * \return Nothing
 */
void sig_pri_chan_delete(struct sig_pri_chan *doomed)
{
	ast_free(doomed);
}

static void build_status(char *s, size_t len, int status, int active)
{
	if (!s || len < 1) {
		return;
	}
	s[0] = '\0';
	if (!(status & DCHAN_NOTINALARM))
		strncat(s, "In Alarm, ", len - strlen(s) - 1);
	if (status & DCHAN_UP)
		strncat(s, "Up", len - strlen(s) - 1);
	else
		strncat(s, "Down", len - strlen(s) - 1);
	if (active)
		strncat(s, ", Active", len - strlen(s) - 1);
	else
		strncat(s, ", Standby", len - strlen(s) - 1);
	s[len - 1] = '\0';
}

void sig_pri_cli_show_spans(int fd, int span, struct sig_pri_pri *pri)
{
	char status[256];
	int x;
	for (x = 0; x < NUM_DCHANS; x++) {
		if (pri->dchans[x]) {
			build_status(status, sizeof(status), pri->dchanavail[x], pri->dchans[x] == pri->pri);
			ast_cli(fd, "PRI span %d/%d: %s\n", span, x, status);
		}
	}
}

void sig_pri_cli_show_span(int fd, int *dchannels, struct sig_pri_pri *pri)
{
	int x;
	char status[256];

	for (x = 0; x < NUM_DCHANS; x++) {
		if (pri->dchans[x]) {
#ifdef PRI_DUMP_INFO_STR
			char *info_str = NULL;
#endif
			ast_cli(fd, "%s D-channel: %d\n", pri_order(x), dchannels[x]);
			build_status(status, sizeof(status), pri->dchanavail[x], pri->dchans[x] == pri->pri);
			ast_cli(fd, "Status: %s\n", status);
#ifdef PRI_DUMP_INFO_STR
			info_str = pri_dump_info_str(pri->pri);
			if (info_str) {
				ast_cli(fd, "%s", info_str);
				free(info_str);
			}
#else
			pri_dump_info(pri->pri);
#endif
			ast_cli(fd, "Overlap Recv: %s\n\n", (pri->overlapdial & DAHDI_OVERLAPDIAL_INCOMING)?"Yes":"No");
			ast_cli(fd, "\n");
		}
	}
}

int pri_send_keypad_facility_exec(struct sig_pri_chan *p, const char *digits)
{
	sig_pri_lock_private(p);

	if (!p->pri || !p->call) {
		ast_debug(1, "Unable to find pri or call on channel!\n");
		sig_pri_unlock_private(p);
		return -1;
	}

	if (!pri_grab(p, p->pri)) {
		pri_keypad_facility(p->pri->pri, p->call, digits);
		pri_rel(p->pri);
	} else {
		ast_debug(1, "Unable to grab pri to send keypad facility!\n");
		sig_pri_unlock_private(p);
		return -1;
	}

	sig_pri_unlock_private(p);

	return 0;
}

int pri_send_callrerouting_facility_exec(struct sig_pri_chan *p, enum ast_channel_state chanstate, const char *destination, const char *original, const char *reason)
{
	int res = -1;

	sig_pri_lock_private(p);

	if (!p->pri || !p->call) {
		ast_log(LOG_DEBUG, "Unable to find pri or call on channel!\n");
		sig_pri_unlock_private(p);
		return -1;
	}

	if (!pri_grab(p, p->pri)) {
		res = pri_callrerouting_facility(p->pri->pri, p->call, destination, original, reason);
		pri_rel(p->pri);
	} else {
		ast_log(LOG_DEBUG, "Unable to grab pri to send callrerouting facility on span %d!\n", p->pri->span);
	}

	sig_pri_unlock_private(p);

	return res;
}

#if defined(HAVE_PRI_SERVICE_MESSAGES)
int pri_maintenance_bservice(struct pri *pri, struct sig_pri_chan *p, int changestatus)
{
	int channel = PVT_TO_CHANNEL(p);
	int span = PRI_SPAN(channel);

	return pri_maintenance_service(pri, span, channel, changestatus);
}
#endif	/* defined(HAVE_PRI_SERVICE_MESSAGES) */

void sig_pri_fixup(struct ast_channel *oldchan, struct ast_channel *newchan, struct sig_pri_chan *pchan)
{
	if (pchan->owner == oldchan) {
		pchan->owner = newchan;
	}
}

#endif /* HAVE_PRI */
