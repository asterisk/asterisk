/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_session</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/callerid.h"
#include "asterisk/conversions.h"

/*!
 * \internal
 * \brief Set an ANI2 integer based on OLI data in a From header
 *
 * This uses the contents of a From header in order to set Originating Line information.
 *
 * \param rdata The incoming message
 * \param ani2 The ANI2 field to set
 * \retval 0 Successfully parsed OLI
 * \retval non-zero Could not parse OLI
 */
static int set_id_from_oli(pjsip_rx_data *rdata, int *ani2)
{
	char oli[AST_CHANNEL_NAME];

	pjsip_param *oli1, *oli2, *oli3;

	static const pj_str_t oli_str1 = { "isup-oli", 8 };
	static const pj_str_t oli_str2 = { "ss7-oli", 7 };
	static const pj_str_t oli_str3 = { "oli", 3 };

	pjsip_fromto_hdr *from = pjsip_msg_find_hdr(rdata->msg_info.msg,
			PJSIP_H_FROM, rdata->msg_info.msg->hdr.next);

	if (!from) {
		return -1; /* This had better not happen */
	}

	if ((oli1 = pjsip_param_find(&from->other_param, &oli_str1))) {
		ast_copy_pj_str(oli, &oli1->value, sizeof(oli));
	} else if ((oli2 = pjsip_param_find(&from->other_param, &oli_str2))) {
		ast_copy_pj_str(oli, &oli2->value, sizeof(oli));
	} else if ((oli3 = pjsip_param_find(&from->other_param, &oli_str3))) {
		ast_copy_pj_str(oli, &oli3->value, sizeof(oli));
	} else {
		return -1;
	}

	return ast_str_to_int(oli, ani2);
}

/*!
 * \internal
 * \brief Determine if a connected line update should be queued
 *
 * This uses information about the session and the ID that would be queued
 * in the connected line update in order to determine if we should queue
 * a connected line update.
 *
 * \param session The session whose channel we wish to queue the connected line update on
 * \param id The identification information that would be queued on the connected line update
 * \retval 0 We should not queue a connected line update
 * \retval non-zero We should queue a connected line update
 */
static int should_queue_connected_line_update(const struct ast_sip_session *session, const struct ast_party_id *id)
{
	/* Invalid number means no update */
	if (!id->number.valid) {
		return 0;
	}

	/* If the session has never communicated an update or if the
	 * new ID has a different number than the session, then we
	 * should queue an update
	 */
	if (ast_strlen_zero(session->id.number.str) ||
			strcmp(session->id.number.str, id->number.str)) {
		return 1;
	}

	/* By making it to this point, it means the number is not enough
	 * to determine if an update should be sent. Now we look at
	 * the name
	 */

	/* If the number couldn't warrant an update and the name is
	 * invalid, then no update
	 */
	if (!id->name.valid) {
		return 0;
	}

	/* If the name has changed or we don't have a name set for the
	 * session, then we should send an update
	 */
	if (ast_strlen_zero(session->id.name.str) ||
			strcmp(session->id.name.str, id->name.str)) {
		return 1;
	}

	/* Neither the name nor the number have changed. No update */
	return 0;
}

/*!
 * \internal
 * \brief Queue a connected line update on a session's channel.
 * \param session The session whose channel should have the connected line update queued upon.
 * \param id The identification information to place in the connected line update
 */
static void queue_connected_line_update(struct ast_sip_session *session, const struct ast_party_id *id)
{
	struct ast_party_connected_line connected;
	struct ast_party_caller caller;

	/* Fill connected line information */
	ast_party_connected_line_init(&connected);
	connected.id = *id;
	connected.id.tag = session->endpoint->id.self.tag;
	connected.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;

	/* Save to channel driver copy */
	ast_party_id_copy(&session->id, &connected.id);

	/* Update our channel CALLERID() */
	ast_party_caller_init(&caller);
	caller.id = connected.id;
	caller.ani = connected.id;
	caller.ani2 = ast_channel_caller(session->channel)->ani2;
	ast_channel_set_caller_event(session->channel, &caller, NULL);

	/* Tell peer about the new connected line information. */
	ast_channel_queue_connected_line_update(session->channel, &connected, NULL);
}

/*!
 * \internal
 * \brief Make updates to connected line information based on an incoming request.
 *
 * This will get identity information from an incoming request. Once the identification is
 * retrieved, we will check if the new information warrants a connected line update and queue
 * a connected line update if so.
 *
 * \param session The session on which we received an incoming request
 * \param rdata The incoming request
 */
static void update_incoming_connected_line(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	struct ast_party_id id;

	if (!session->endpoint->id.trust_connected_line
		|| !session->endpoint->id.trust_inbound) {
		return;
	}

	ast_party_id_init(&id);
	if (!ast_sip_set_id_connected_line(rdata, &id)) {
		if (should_queue_connected_line_update(session, &id)) {
			queue_connected_line_update(session, &id);
		}
	}
	ast_party_id_free(&id);
}

/*!
 * \internal
 * \brief Session supplement callback on an incoming INVITE request
 *
 * If we are receiving an initial INVITE, then we will set the session's identity
 * based on the INVITE or configured endpoint values. If we are receiving a reinvite,
 * then we will potentially queue a connected line update via the \ref update_incoming_connected_line
 * function
 *
 * \param session The session that has received an INVITE
 * \param rdata The incoming INVITE
 */
static int caller_id_incoming_request(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	if (!session->channel) {
		int ani2;
		/*
		 * Since we have no channel this must be the initial inbound
		 * INVITE.  Set the session ID directly because the channel
		 * has not been created yet.
		 */
		ast_sip_set_id_from_invite(rdata, &session->id, &session->endpoint->id.self, session->endpoint->id.trust_inbound);
		session->ani2 = set_id_from_oli(rdata, &ani2) ? 0 : ani2;
	} else {
		/*
		 * ReINVITE or UPDATE.  Check for changes to the ID and queue
		 * a connected line update if necessary.
		 */
		update_incoming_connected_line(session, rdata);
	}
	return 0;
}

/*!
 * \internal
 * \brief Session supplement callback on INVITE response
 *
 * INVITE responses could result in queuing connected line updates.
 *
 * \param session The session on which communication is happening
 * \param rdata The incoming INVITE response
 */
static void caller_id_incoming_response(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	if (!session->channel) {
		return;
	}

	update_incoming_connected_line(session, rdata);
}

/*!
 * \internal
 * \brief Create an identity header for an outgoing message
 * \param hdr_name The name of the header to create
 * \param base
 * \param tdata The message to place the header on
 * \param id The identification information for the new header
 * \return newly-created header
 */
static pjsip_fromto_hdr *create_new_id_hdr(const pj_str_t *hdr_name, pjsip_fromto_hdr *base, pjsip_tx_data *tdata, const struct ast_party_id *id)
{
	pjsip_fromto_hdr *id_hdr;
	pjsip_name_addr *id_name_addr;
	pjsip_sip_uri *id_uri;

	id_hdr = pjsip_from_hdr_create(tdata->pool);
	id_hdr->type = PJSIP_H_OTHER;
	id_hdr->sname = id_hdr->name = *hdr_name;

	id_name_addr = pjsip_uri_clone(tdata->pool, base->uri);
	id_uri = pjsip_uri_get_uri(id_name_addr->uri);

	if (id->name.valid && !ast_strlen_zero(id->name.str)) {
		int name_buf_len = strlen(id->name.str) * 2 + 1;
		char *name_buf = ast_alloca(name_buf_len);

		ast_escape_quoted(id->name.str, name_buf, name_buf_len);
		pj_strdup2(tdata->pool, &id_name_addr->display, name_buf);
	} else {
		/*
		 * We need to clear the remnants of the clone or it'll be left set.
		 * pj_strdup2 is safe to call with a NULL src and it resets both slen and ptr.
		 */
		pj_strdup2(tdata->pool, &id_name_addr->display, NULL);
	}

	if (id->number.valid) {
		pj_strdup2(tdata->pool, &id_uri->user, id->number.str);
	} else {
		/* Similar to name, make sure the number is also cleared when invalid */
		pj_strdup2(tdata->pool, &id_uri->user, NULL);
	}

	id_hdr->uri = (pjsip_uri *) id_name_addr;
	return id_hdr;
}

/*!
 * \internal
 * \brief Add a Privacy header to an outbound message
 *
 * When sending a P-Asserted-Identity header, if privacy is requested, then we
 * will need to indicate such by adding a Privacy header. Similarly, if no
 * privacy is requested, and a Privacy header already exists on the message,
 * then the old Privacy header should be removed.
 *
 * \param tdata The outbound message to add the Privacy header to
 * \param id The id information used to determine privacy
 */
static void add_privacy_header(pjsip_tx_data *tdata, const struct ast_party_id *id)
{
	static const pj_str_t pj_privacy_name = { "Privacy", 7 };
	static const pj_str_t pj_privacy_value = { "id", 2 };
	pjsip_hdr *old_privacy;

	old_privacy = pjsip_msg_find_hdr_by_name(tdata->msg, &pj_privacy_name, NULL);

	if ((ast_party_id_presentation(id) & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED) {
		if (old_privacy) {
			pj_list_erase(old_privacy);
		}
	} else if (!old_privacy) {
		pjsip_generic_string_hdr *privacy_hdr = pjsip_generic_string_hdr_create(
				tdata->pool, &pj_privacy_name, &pj_privacy_value);
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *)privacy_hdr);
	}
}

/*!
 * \internal
 * \brief Add a P-Asserted-Identity header to an outbound message
 * \param session The session on which communication is happening
 * \param tdata The message to add the header to
 * \param id The identification information used to populate the header
 */
static void add_pai_header(const struct ast_sip_session *session, pjsip_tx_data *tdata, const struct ast_party_id *id)
{
	static const pj_str_t pj_pai_name = { "P-Asserted-Identity", 19 };
	pjsip_fromto_hdr *base;
	pjsip_fromto_hdr *pai_hdr;
	pjsip_fromto_hdr *old_pai;

	/* Since inv_session reuses responses, we have to make sure there's not already
	 * a P-Asserted-Identity present. If there is, we just modify the old one.
	 */
	old_pai = pjsip_msg_find_hdr_by_name(tdata->msg, &pj_pai_name, NULL);
	if (old_pai) {
		/* If type is OTHER, then the existing header was most likely
		 * added by the PJSIP_HEADER dial plan function as a simple
		 * name/value pair.  We can't pass this to modify_id_header because
		 * there are no virtual functions to get the uri.  We could parse
		 * it into a pjsip_fromto_hdr but it isn't worth it since
		 * modify_id_header is just going to overwrite the name and number
		 * anyway.  We'll just remove it from the header list instead
		 * and create a new one.
		 */
		if (old_pai->type == PJSIP_H_OTHER) {
			pj_list_erase(old_pai);
		} else {
			ast_sip_modify_id_header(tdata->pool, old_pai, id);
			add_privacy_header(tdata, id);
			return;
		}
	}

	if (tdata->msg->type == PJSIP_REQUEST_MSG) {
		base = session->saved_from_hdr ? session->saved_from_hdr : PJSIP_MSG_FROM_HDR(tdata->msg);
	} else {
		base = PJSIP_MSG_TO_HDR(tdata->msg);
	}

	pai_hdr = create_new_id_hdr(&pj_pai_name, base, tdata, id);
	if (!pai_hdr) {
		return;
	}
	add_privacy_header(tdata, id);

	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *)pai_hdr);
}

/*!
 * \internal
 * \brief Add party parameter to a Remote-Party-ID header.
 *
 * \param tdata The message where the Remote-Party-ID header is
 * \param hdr The header on which the parameters are being added
 * \param session The session involved
 */
static void add_party_param(pjsip_tx_data *tdata, pjsip_fromto_hdr *hdr, const struct ast_sip_session *session)
{
	static const pj_str_t party_str = { "party", 5 };
	static const pj_str_t calling_str = { "calling", 7 };
	static const pj_str_t called_str = { "called", 6 };
	pjsip_param *party;

	/* The party value can't change throughout the lifetime, so it is set only once */
	party = pjsip_param_find(&hdr->other_param, &party_str);
	if (party) {
		return;
	}

	party = PJ_POOL_ALLOC_T(tdata->pool, pjsip_param);
	party->name = party_str;
	party->value = (session->inv_session->role == PJSIP_ROLE_UAC) ? calling_str : called_str;
	pj_list_insert_before(&hdr->other_param, party);
}

/*!
 * \internal
 * \brief Add privacy and screen parameters to a Remote-Party-ID header.
 *
 * If privacy is requested, then the privacy and screen parameters need to
 * reflect this. Similarly, if no privacy or screening is to be communicated,
 * we need to make sure that any previously set values are updated.
 *
 * \param tdata The message where the Remote-Party-ID header is
 * \param hdr The header on which the parameters are being added
 * \param id The identification information used to determine privacy
 */
static void add_privacy_params(pjsip_tx_data *tdata, pjsip_fromto_hdr *hdr, const struct ast_party_id *id)
{
	static const pj_str_t privacy_str = { "privacy", 7 };
	static const pj_str_t screen_str = { "screen", 6 };
	static const pj_str_t privacy_full_str = { "full", 4 };
	static const pj_str_t privacy_off_str = { "off", 3 };
	static const pj_str_t screen_yes_str = { "yes", 3 };
	static const pj_str_t screen_no_str = { "no", 2 };
	pjsip_param *old_privacy;
	pjsip_param *old_screen;
	pjsip_param *privacy;
	pjsip_param *screen;
	int presentation;

	old_privacy = pjsip_param_find(&hdr->other_param, &privacy_str);
	old_screen = pjsip_param_find(&hdr->other_param, &screen_str);

	if (!old_privacy) {
		privacy = PJ_POOL_ALLOC_T(tdata->pool, pjsip_param);
		privacy->name = privacy_str;
		pj_list_insert_before(&hdr->other_param, privacy);
	} else {
		privacy = old_privacy;
	}

	if (!old_screen) {
		screen = PJ_POOL_ALLOC_T(tdata->pool, pjsip_param);
		screen->name = screen_str;
		pj_list_insert_before(&hdr->other_param, screen);
	} else {
		screen = old_screen;
	}

	presentation = ast_party_id_presentation(id);
	if ((presentation & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED) {
		privacy->value = privacy_off_str;
	} else {
		privacy->value = privacy_full_str;
	}
	if ((presentation & AST_PRES_NUMBER_TYPE) == AST_PRES_USER_NUMBER_PASSED_SCREEN) {
		screen->value = screen_yes_str;
	} else {
		screen->value = screen_no_str;
	}
}

/*!
 * \internal
 * \brief Add a Remote-Party-ID header to an outbound message
 * \param session The session on which communication is happening
 * \param tdata The message to add the header to
 * \param id The identification information used to populate the header
 */
static void add_rpid_header(const struct ast_sip_session *session, pjsip_tx_data *tdata, const struct ast_party_id *id)
{
	static const pj_str_t pj_rpid_name = { "Remote-Party-ID", 15 };
	pjsip_fromto_hdr *base;
	pjsip_fromto_hdr *rpid_hdr;
	pjsip_fromto_hdr *old_rpid;

	/* Since inv_session reuses responses, we have to make sure there's not already
	 * a P-Asserted-Identity present. If there is, we just modify the old one.
	 */
	old_rpid = pjsip_msg_find_hdr_by_name(tdata->msg, &pj_rpid_name, NULL);
	if (old_rpid) {
		/* If type is OTHER, then the existing header was most likely
		 * added by the PJSIP_HEADER dial plan function as a simple
		 * name/value pair.  We can't pass this to modify_id_header because
		 * there are no virtual functions to get the uri.  We could parse
		 * it into a pjsip_fromto_hdr but it isn't worth it since
		 * modify_id_header is just going to overwrite the name and number
		 * anyway.  We'll just remove it from the header list instead
		 * and create a new one.
		 */
		if (old_rpid->type == PJSIP_H_OTHER) {
			pj_list_erase(old_rpid);
		} else {
			ast_sip_modify_id_header(tdata->pool, old_rpid, id);
			add_party_param(tdata, old_rpid, session);
			add_privacy_params(tdata, old_rpid, id);
			return;
		}
	}

	if (tdata->msg->type == PJSIP_REQUEST_MSG) {
		base = session->saved_from_hdr ? session->saved_from_hdr : PJSIP_MSG_FROM_HDR(tdata->msg);
	} else {
		base = PJSIP_MSG_TO_HDR(tdata->msg);
	}

	rpid_hdr = create_new_id_hdr(&pj_rpid_name, base, tdata, id);
	if (!rpid_hdr) {
		return;
	}
	add_party_param(tdata, rpid_hdr, session);
	add_privacy_params(tdata, rpid_hdr, id);
	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *)rpid_hdr);
}

/*!
 * \internal
 * \brief Add any appropriate identification headers to an outbound SIP message
 *
 * This will determine if an outbound message should have identification headers and
 * will add the appropriately configured headers
 *
 * \param session The session on which we will be sending the message
 * \param tdata The outbound message
 * \param id The identity information to place on the message
 */
static void add_id_headers(const struct ast_sip_session *session, pjsip_tx_data *tdata, const struct ast_party_id *id)
{
	if (!id->number.valid
		|| (!session->endpoint->id.trust_outbound
			&& (ast_party_id_presentation(id) & AST_PRES_RESTRICTION) != AST_PRES_ALLOWED)) {
		return;
	}
	if (session->endpoint->id.send_pai) {
		add_pai_header(session, tdata, id);
	}
	if (session->endpoint->id.send_rpid) {
		add_rpid_header(session, tdata, id);
	}
}

/*!
 * \internal
 * \brief Session supplement callback for outgoing INVITE requests
 *
 * On all INVITEs (initial and reinvite) we may add other identity headers
 * such as P-Asserted-Identity and Remote-Party-ID based on configuration
 * and privacy settings
 *
 * \param session The session on which the INVITE will be sent
 * \param tdata The outbound INVITE request
 */
static void caller_id_outgoing_request(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	struct ast_party_id effective_id;
	struct ast_party_id connected_id;

	if (!session->channel) {
		return;
	}

	ast_party_id_init(&connected_id);
	ast_channel_lock(session->channel);
	effective_id = ast_channel_connected_effective_id(session->channel);
	ast_party_id_copy(&connected_id, &effective_id);
	ast_channel_unlock(session->channel);

	add_id_headers(session, tdata, &connected_id);
	ast_party_id_free(&connected_id);
}

/*!
 * \internal
 * \brief Session supplement for outgoing INVITE response
 *
 * This will add P-Asserted-Identity and Remote-Party-ID headers if necessary
 *
 * \param session The session on which the INVITE response is to be sent
 * \param tdata The outbound INVITE response
 */
static void caller_id_outgoing_response(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	struct ast_party_id effective_id;
	struct ast_party_id connected_id;

	if (!session->channel
		|| (!session->endpoint->id.send_connected_line
			&& session->inv_session
			&& session->inv_session->state >= PJSIP_INV_STATE_EARLY)) {
		return;
	}

	/* Must do a deep copy unless we hold the channel lock the entire time. */
	ast_party_id_init(&connected_id);
	ast_channel_lock(session->channel);
	effective_id = ast_channel_connected_effective_id(session->channel);
	ast_party_id_copy(&connected_id, &effective_id);
	ast_channel_unlock(session->channel);

	add_id_headers(session, tdata, &connected_id);
	ast_party_id_free(&connected_id);
}

static struct ast_sip_session_supplement caller_id_supplement = {
	.method = "INVITE,UPDATE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL - 1000,
	.incoming_request = caller_id_incoming_request,
	.incoming_response = caller_id_incoming_response,
	.outgoing_request = caller_id_outgoing_request,
	.outgoing_response = caller_id_outgoing_response,
};

static int load_module(void)
{
	ast_module_shutdown_ref(AST_MODULE_SELF);
	ast_sip_session_register_supplement(&caller_id_supplement);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&caller_id_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Caller ID Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip,res_pjsip_session",
);
