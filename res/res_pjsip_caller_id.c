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

/*!
 * \internal
 * \brief Set an ast_party_id name and number based on an identity header.
 * \param hdr From, P-Asserted-Identity, or Remote-Party-ID header on incoming message
 * \param[out] id The ID to set data on
 */
static void set_id_from_hdr(pjsip_fromto_hdr *hdr, struct ast_party_id *id)
{
	char cid_name[AST_CHANNEL_NAME];
	char cid_num[AST_CHANNEL_NAME];
	pjsip_sip_uri *uri;
	pjsip_name_addr *id_name_addr = (pjsip_name_addr *) hdr->uri;

	uri = pjsip_uri_get_uri(id_name_addr);
	ast_copy_pj_str(cid_name, &id_name_addr->display, sizeof(cid_name));
	ast_copy_pj_str(cid_num, &uri->user, sizeof(cid_num));

	ast_free(id->name.str);
	id->name.str = ast_strdup(cid_name);
	if (!ast_strlen_zero(cid_name)) {
		id->name.valid = 1;
	}
	ast_free(id->number.str);
	id->number.str = ast_strdup(cid_num);
	if (!ast_strlen_zero(cid_num)) {
		id->number.valid = 1;
	}
}

/*!
 * \internal
 * \brief Get a P-Asserted-Identity or Remote-Party-ID header from an incoming message
 *
 * This function will parse the header as if it were a From header. This allows for us
 * to easily manipulate the URI, as well as add, modify, or remove parameters from the
 * header
 *
 * \param rdata The incoming message
 * \param header_name The name of the ID header to find
 * \retval NULL No ID header present or unable to parse ID header
 * \retval non-NULL The parsed ID header
 */
static pjsip_fromto_hdr *get_id_header(pjsip_rx_data *rdata, const pj_str_t *header_name)
{
	static const pj_str_t from = { "From", 4 };
	pj_str_t header_content;
	pjsip_fromto_hdr *parsed_hdr;
	pjsip_generic_string_hdr *ident = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg,
			header_name, NULL);
	int parsed_len;

	if (!ident) {
		return NULL;
	}

	pj_strdup_with_null(rdata->tp_info.pool, &header_content, &ident->hvalue);

	parsed_hdr = pjsip_parse_hdr(rdata->tp_info.pool, &from, header_content.ptr,
			pj_strlen(&header_content), &parsed_len);

	if (!parsed_hdr) {
		return NULL;
	}

	return parsed_hdr;
}

/*!
 * \internal
 * \brief Set an ast_party_id structure based on data in a P-Asserted-Identity header
 *
 * This makes use of \ref set_id_from_hdr for setting name and number. It uses
 * the contents of a Privacy header in order to set presentation information.
 *
 * \param rdata The incoming message
 * \param[out] id The ID to set
 * \retval 0 Successfully set the party ID
 * \retval non-zero Could not set the party ID
 */
static int set_id_from_pai(pjsip_rx_data *rdata, struct ast_party_id *id)
{
	static const pj_str_t pai_str = { "P-Asserted-Identity", 19 };
	static const pj_str_t privacy_str = { "Privacy", 7 };
	pjsip_fromto_hdr *pai_hdr = get_id_header(rdata, &pai_str);
	pjsip_generic_string_hdr *privacy;

	if (!pai_hdr) {
		return -1;
	}

	set_id_from_hdr(pai_hdr, id);

	if (!id->number.valid) {
		return -1;
	}

	privacy = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &privacy_str, NULL);
	if (!privacy) {
		return 0;
	}
	if (!pj_stricmp2(&privacy->hvalue, "id")) {
		id->number.presentation = AST_PRES_PROHIB_USER_NUMBER_NOT_SCREENED;
		id->name.presentation = AST_PRES_PROHIB_USER_NUMBER_NOT_SCREENED;
	}

	return 0;
}

/*!
 * \internal
 * \brief Set an ast_party_id structure based on data in a Remote-Party-ID header
 *
 * This makes use of \ref set_id_from_hdr for setting name and number. It uses
 * the privacy and screen parameters in order to set presentation information.
 *
 * \param rdata The incoming message
 * \param[out] id The ID to set
 * \retval 0 Succesfully set the party ID
 * \retval non-zero Could not set the party ID
 */
static int set_id_from_rpid(pjsip_rx_data *rdata, struct ast_party_id *id)
{
	static const pj_str_t rpid_str = { "Remote-Party-ID", 15 };
	static const pj_str_t privacy_str = { "privacy", 7 };
	static const pj_str_t screen_str = { "screen", 6 };
	pjsip_fromto_hdr *rpid_hdr = get_id_header(rdata, &rpid_str);
	pjsip_param *screen;
	pjsip_param *privacy;

	if (!rpid_hdr) {
		return -1;
	}

	set_id_from_hdr(rpid_hdr, id);

	if (!id->number.valid) {
		return -1;
	}

	privacy = pjsip_param_find(&rpid_hdr->other_param, &privacy_str);
	screen = pjsip_param_find(&rpid_hdr->other_param, &screen_str);
	if (privacy && !pj_stricmp2(&privacy->value, "full")) {
		id->number.presentation |= AST_PRES_RESTRICTED;
		id->name.presentation |= AST_PRES_RESTRICTED;
	}
	if (screen && !pj_stricmp2(&screen->value, "yes")) {
		id->number.presentation |= AST_PRES_USER_NUMBER_PASSED_SCREEN;
		id->name.presentation |= AST_PRES_USER_NUMBER_PASSED_SCREEN;
	}

	return 0;
}

/*!
 * \internal
 * \brief Set an ast_party_id structure based on data in a From
 *
 * This makes use of \ref set_id_from_hdr for setting name and number. It uses
 * no information from the message in order to set privacy. It relies on endpoint
 * configuration for privacy information.
 *
 * \param rdata The incoming message
 * \param[out] id The ID to set
 * \retval 0 Succesfully set the party ID
 * \retval non-zero Could not set the party ID
 */
static int set_id_from_from(struct pjsip_rx_data *rdata, struct ast_party_id *id)
{
	pjsip_fromto_hdr *from = pjsip_msg_find_hdr(rdata->msg_info.msg,
			PJSIP_H_FROM, rdata->msg_info.msg->hdr.next);

	if (!from) {
		/* This had better not happen */
		return -1;
	}

	set_id_from_hdr(from, id);

	if (!id->number.valid) {
		return -1;
	}

	return 0;
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

	if (!session->endpoint->id.trust_inbound) {
		return;
	}

	ast_party_id_init(&id);
	if (!set_id_from_pai(rdata, &id) || !set_id_from_rpid(rdata, &id)) {
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
	if (session->inv_session->state < PJSIP_INV_STATE_CONFIRMED) {
		/*
		 * Initial inbound INVITE.  Set the session ID directly
		 * because the channel has not been created yet.
		 */
		if (session->endpoint->id.trust_inbound
			&& (!set_id_from_pai(rdata, &session->id)
				|| !set_id_from_rpid(rdata, &session->id))) {
			ast_free(session->id.tag);
			session->id.tag = ast_strdup(session->endpoint->id.self.tag);
			return 0;
		}
		ast_party_id_copy(&session->id, &session->endpoint->id.self);
		if (!session->endpoint->id.self.number.valid) {
			set_id_from_from(rdata, &session->id);
		}
	} else {
		/* Reinvite. Check for changes to the ID and queue a connected line
		 * update if necessary
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
 * \brief Set name and number information on an identity header.
 * \param pool Memory pool to use for string duplication
 * \param id_hdr A From, P-Asserted-Identity, or Remote-Party-ID header to modify
 * \param id The identity information to apply to the header
 */
static void modify_id_header(pj_pool_t *pool, pjsip_fromto_hdr *id_hdr, const struct ast_party_id *id)
{
	pjsip_name_addr *id_name_addr;
	pjsip_sip_uri *id_uri;

	id_name_addr = (pjsip_name_addr *) id_hdr->uri;
	id_uri = pjsip_uri_get_uri(id_name_addr->uri);

	if (id->name.valid) {
		pj_strdup2(pool, &id_name_addr->display, id->name.str);
	}

	if (id->number.valid) {
		pj_strdup2(pool, &id_uri->user, id->number.str);
	}
}

/*!
 * \internal
 * \brief Create an identity header for an outgoing message
 * \param hdr_name The name of the header to create
 * \param tdata The message to place the header on
 * \param id The identification information for the new header
 * \return newly-created header
 */
static pjsip_fromto_hdr *create_new_id_hdr(const pj_str_t *hdr_name, pjsip_tx_data *tdata, const struct ast_party_id *id)
{
	pjsip_fromto_hdr *id_hdr;
	pjsip_fromto_hdr *base;
	pjsip_name_addr *id_name_addr;
	pjsip_sip_uri *id_uri;

	base = tdata->msg->type == PJSIP_REQUEST_MSG ? PJSIP_MSG_FROM_HDR(tdata->msg) :
		PJSIP_MSG_TO_HDR(tdata->msg);
	id_hdr = pjsip_from_hdr_create(tdata->pool);
	id_hdr->type = PJSIP_H_OTHER;
	pj_strdup(tdata->pool, &id_hdr->name, hdr_name);
	id_hdr->sname.slen = 0;

	id_name_addr = pjsip_uri_clone(tdata->pool, base->uri);
	id_uri = pjsip_uri_get_uri(id_name_addr->uri);

	if (id->name.valid) {
		pj_strdup2(tdata->pool, &id_name_addr->display, id->name.str);
	}

	pj_strdup2(tdata->pool, &id_uri->user, id->number.str);

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

	if ((id->name.presentation & AST_PRES_RESTRICTION) == AST_PRES_RESTRICTED ||
			(id->name.presentation & AST_PRES_RESTRICTION) == AST_PRES_RESTRICTED) {
		if (!old_privacy) {
			pjsip_generic_string_hdr *privacy_hdr = pjsip_generic_string_hdr_create(
					tdata->pool, &pj_privacy_name, &pj_privacy_value);
			pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *)privacy_hdr);
		}
	} else {
		if (old_privacy) {
			pj_list_erase(old_privacy);
		}
	}
}

/*!
 * \internal
 * \brief Add a P-Asserted-Identity header to an outbound message
 * \param tdata The message to add the header to
 * \param id The identification information used to populate the header
 */
static void add_pai_header(pjsip_tx_data *tdata, const struct ast_party_id *id)
{
	static const pj_str_t pj_pai_name = { "P-Asserted-Identity", 19 };
	pjsip_fromto_hdr *pai_hdr;
	pjsip_fromto_hdr *old_pai;

	if (!id->number.valid) {
		return;
	}

	/* Since inv_session reuses responses, we have to make sure there's not already
	 * a P-Asserted-Identity present. If there is, we just modify the old one.
	 */
	old_pai = pjsip_msg_find_hdr_by_name(tdata->msg, &pj_pai_name, NULL);
	if (old_pai) {
		modify_id_header(tdata->pool, old_pai, id);
		add_privacy_header(tdata, id);
		return;
	}

	pai_hdr = create_new_id_hdr(&pj_pai_name, tdata, id);
	if (!pai_hdr) {
		return;
	}
	add_privacy_header(tdata, id);

	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *)pai_hdr);
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

	if ((id->name.presentation & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED &&
			(id->name.presentation & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED) {
		privacy->value = privacy_off_str;
	} else {
		privacy->value = privacy_full_str;
	}

	if ((id->name.presentation & AST_PRES_NUMBER_TYPE) == AST_PRES_USER_NUMBER_PASSED_SCREEN &&
			(id->number.presentation & AST_PRES_NUMBER_TYPE) == AST_PRES_USER_NUMBER_PASSED_SCREEN) {
		screen->value = screen_yes_str;
	} else {
		screen->value = screen_no_str;
	}
}

/*!
 * \internal
 * \brief Add a Remote-Party-ID header to an outbound message
 * \param tdata The message to add the header to
 * \param id The identification information used to populate the header
 */
static void add_rpid_header(pjsip_tx_data *tdata, const struct ast_party_id *id)
{
	static const pj_str_t pj_rpid_name = { "Remote-Party-ID", 15 };
	pjsip_fromto_hdr *rpid_hdr;
	pjsip_fromto_hdr *old_rpid;

	if (!id->number.valid) {
		return;
	}

	/* Since inv_session reuses responses, we have to make sure there's not already
	 * a P-Asserted-Identity present. If there is, we just modify the old one.
	 */
	old_rpid = pjsip_msg_find_hdr_by_name(tdata->msg, &pj_rpid_name, NULL);
	if (old_rpid) {
		modify_id_header(tdata->pool, old_rpid, id);
		add_privacy_params(tdata, old_rpid, id);
		return;
	}

	rpid_hdr = create_new_id_hdr(&pj_rpid_name, tdata, id);
	if (!rpid_hdr) {
		return;
	}
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
 * \param The identity information to place on the message
 */
static void add_id_headers(const struct ast_sip_session *session, pjsip_tx_data *tdata, const struct ast_party_id *id)
{
	if (((id->name.presentation & AST_PRES_RESTRICTION) == AST_PRES_RESTRICTED ||
			(id->number.presentation & AST_PRES_RESTRICTION) == AST_PRES_RESTRICTED) &&
			!session->endpoint->id.trust_outbound) {
		return;
	}
	if (session->endpoint->id.send_pai) {
		add_pai_header(tdata, id);
	}
	if (session->endpoint->id.send_rpid) {
		add_rpid_header(tdata, id);
	}
}

/*!
 * \internal
 * \brief Session supplement callback for outgoing INVITE requests
 *
 * For an initial INVITE request, we may change the From header to appropriately
 * reflect the identity information. On all INVITEs (initial and reinvite) we may
 * add other identity headers such as P-Asserted-Identity and Remote-Party-ID based
 * on configuration and privacy settings
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

	/* Must do a deep copy unless we hold the channel lock the entire time. */
	ast_party_id_init(&connected_id);
	ast_channel_lock(session->channel);
	effective_id = ast_channel_connected_effective_id(session->channel);
	ast_party_id_copy(&connected_id, &effective_id);
	ast_channel_unlock(session->channel);

	if (session->inv_session->state < PJSIP_INV_STATE_CONFIRMED &&
			ast_strlen_zero(session->endpoint->fromuser) &&
			(session->endpoint->id.trust_outbound ||
			((connected_id.name.presentation & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED &&
			(connected_id.number.presentation & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED))) {
		/* Only change the From header on the initial outbound INVITE. Switching it
		 * mid-call might confuse some UAs.
		 */
		pjsip_fromto_hdr *from;
		pjsip_dialog *dlg;

		from = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_FROM, tdata->msg->hdr.next);
		dlg = session->inv_session->dlg;

		modify_id_header(tdata->pool, from, &connected_id);
		modify_id_header(dlg->pool, dlg->local.info, &connected_id);
	}
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

	if (!session->channel) {
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
	       );
