/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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
#include "asterisk/callerid.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/strings.h"

static const pj_str_t diversion_name = { "Diversion", 9 };

/*!
 * \internal
 * \brief Determine if the given string is a SIP token.
 * \since 13.8.0
 *
 * \param str String to determine if is a SIP token.
 *
 * \note A token is defined by RFC3261 Section 25.1
 *
 * \return Non-zero if the string is a SIP token.
 */
static int sip_is_token(const char *str)
{
	int is_token;

	if (ast_strlen_zero(str)) {
		/* An empty string is not a token. */
		return 0;
	}

	is_token = 1;
	do {
		if (!isalnum(*str)
			&& !strchr("-.!%*_+`'~", *str)) {
			/* The character is not allowed in a token. */
			is_token = 0;
			break;
		}
	} while (*++str);

	return is_token;
}

/*! \brief Diversion header reasons
 *
 * The core defines a bunch of constants used to define
 * redirecting reasons. This provides a translation table
 * between those and the strings which may be present in
 * a SIP Diversion header
 */
static const struct reasons {
	enum AST_REDIRECTING_REASON code;
	const char *text;
} reason_table[] = {
	{ AST_REDIRECTING_REASON_UNKNOWN, "unknown" },
	{ AST_REDIRECTING_REASON_USER_BUSY, "user-busy" },
	{ AST_REDIRECTING_REASON_NO_ANSWER, "no-answer" },
	{ AST_REDIRECTING_REASON_UNAVAILABLE, "unavailable" },
	{ AST_REDIRECTING_REASON_UNCONDITIONAL, "unconditional" },
	{ AST_REDIRECTING_REASON_TIME_OF_DAY, "time-of-day" },
	{ AST_REDIRECTING_REASON_DO_NOT_DISTURB, "do-not-disturb" },
	{ AST_REDIRECTING_REASON_DEFLECTION, "deflection" },
	{ AST_REDIRECTING_REASON_FOLLOW_ME, "follow-me" },
	{ AST_REDIRECTING_REASON_OUT_OF_ORDER, "out-of-service" },
	{ AST_REDIRECTING_REASON_AWAY, "away" },
	{ AST_REDIRECTING_REASON_CALL_FWD_DTE, "cf_dte" },		/* Non-standard */
	{ AST_REDIRECTING_REASON_SEND_TO_VM, "send_to_vm" },	/* Non-standard */
};

static const char *reason_code_to_str(const struct ast_party_redirecting_reason *reason)
{
	int idx;
	int code;

	/* use specific string if given */
	if (!ast_strlen_zero(reason->str)) {
		return reason->str;
	}

	code = reason->code;
	for (idx = 0; idx < ARRAY_LEN(reason_table); ++idx) {
		if (code == reason_table[idx].code) {
			return reason_table[idx].text;
		}
	}

	return "unknown";
}

static pjsip_fromto_hdr *get_diversion_header(pjsip_rx_data *rdata)
{
	static const pj_str_t from_name = { "From", 4 };

	pjsip_generic_string_hdr *hdr;
	pj_str_t value;
	int size;

	if (!(hdr = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &diversion_name, NULL))) {
		return NULL;
	}

	pj_strdup_with_null(rdata->tp_info.pool, &value, &hdr->hvalue);

	/* parse as a fromto header */
	return pjsip_parse_hdr(rdata->tp_info.pool, &from_name, value.ptr,
			       pj_strlen(&value), &size);
}

static void set_redirecting_value(char **dst, const pj_str_t *src)
{
	ast_free(*dst);
	*dst = ast_malloc(pj_strlen(src) + 1);
	if (*dst) {
		ast_copy_pj_str(*dst, src, pj_strlen(src) + 1);
	}
}

static void set_redirecting_id(pjsip_name_addr *name_addr, struct ast_party_id *data,
			       struct ast_set_party_id *update)
{
	pjsip_sip_uri *uri = pjsip_uri_get_uri(name_addr->uri);
	char *semi;
	pj_str_t uri_user;

	uri_user = uri->user;

	/* Always truncate redirecting number at a semicolon. */
	semi = pj_strchr(&uri_user, ';');
	if (semi) {
		/*
		 * We need to be able to handle URI's looking like
		 * "sip:1235557890;phone-context=national@x.x.x.x;user=phone"
		 *
		 * Where the uri->user field will result in:
		 * "1235557890;phone-context=national"
		 *
		 * People don't care about anything after the semicolon
		 * showing up on their displays even though the RFC
		 * allows the semicolon.
		 */
		pj_strset(&uri_user, (char *) pj_strbuf(&uri_user), semi - pj_strbuf(&uri_user));
	}

	if (pj_strlen(&uri_user)) {
		update->number = 1;
		data->number.valid = 1;
		set_redirecting_value(&data->number.str, &uri_user);
	}

	if (pj_strlen(&name_addr->display)) {
		update->name = 1;
		data->name.valid = 1;
		set_redirecting_value(&data->name.str, &name_addr->display);
	}
}

static void copy_redirecting_id(struct ast_party_id *dst, const struct ast_party_id *src,
				struct ast_set_party_id *update)
{
	ast_party_id_copy(dst, src);

	if (dst->number.valid) {
		update->number = 1;
	}

	if (dst->name.valid) {
		update->name = 1;
	}
}

static void set_redirecting_reason(pjsip_fromto_hdr *hdr,
				   struct ast_party_redirecting_reason *data)
{
	static const pj_str_t reason_name = { "reason", 6 };
	pjsip_param *reason = pjsip_param_find(&hdr->other_param, &reason_name);
	char *reason_str;

	if (!reason) {
		return;
	}

	set_redirecting_value(&data->str, &reason->value);
	if (!data->str) {
		/* Oops, allocation failure */
		return;
	}
	reason_str = ast_strdupa(data->str);

	/* Remove any enclosing double-quotes */
	if (*reason_str == '"') {
		reason_str = ast_strip_quoted(reason_str, "\"", "\"");
	}

	data->code = ast_redirecting_reason_parse(reason_str);
	if (data->code < 0) {
		data->code = AST_REDIRECTING_REASON_UNKNOWN;
	} else {
		ast_free(data->str);
		data->str = ast_strdup("");
	}
}

static void set_redirecting(struct ast_sip_session *session,
			    pjsip_fromto_hdr *from_info,
			    pjsip_name_addr *to_info)
{
	struct ast_party_redirecting data;
	struct ast_set_party_redirecting update;

	if (!session->channel) {
		return;
	}

	ast_party_redirecting_init(&data);
	memset(&update, 0, sizeof(update));

	if (from_info) {
		set_redirecting_id((pjsip_name_addr*)from_info->uri,
			&data.from, &update.from);
		set_redirecting_reason(from_info, &data.reason);
	} else {
		copy_redirecting_id(&data.from, &session->id, &update.from);
	}

	set_redirecting_id(to_info, &data.to, &update.to);

	ast_set_party_id_all(&update.priv_orig);
	ast_set_party_id_all(&update.priv_from);
	ast_set_party_id_all(&update.priv_to);
	++data.count;

	ast_channel_set_redirecting(session->channel, &data, &update);
	ast_party_redirecting_free(&data);
}

static int diversion_incoming_request(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	pjsip_fromto_hdr *hdr = get_diversion_header(rdata);

	if (hdr) {
		set_redirecting(session, hdr, (pjsip_name_addr*)
				PJSIP_MSG_TO_HDR(rdata->msg_info.msg)->uri);
	}

	return 0;
}

static void diversion_incoming_response(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	static const pj_str_t contact_name = { "Contact", 7 };
	static const pj_str_t contact_name_s = { "m", 1 };

	pjsip_status_line status = rdata->msg_info.msg->line.status;
	pjsip_fromto_hdr *div_hdr;
	pjsip_contact_hdr *contact_hdr;

	if ((status.code != 302) && (status.code != 181)) {
		return;
	}

	/* use the diversion header info if there is one. if not one then use the
           session caller id info. if that doesn't exist use info from the To hdr*/
	if (!(div_hdr = get_diversion_header(rdata)) && !session->id.number.valid) {
		div_hdr = PJSIP_MSG_TO_HDR(rdata->msg_info.msg);
	}

	contact_hdr = pjsip_msg_find_hdr_by_names(rdata->msg_info.msg, &contact_name, &contact_name_s, NULL);

	set_redirecting(session, div_hdr, contact_hdr ?	(pjsip_name_addr*)contact_hdr->uri :
			(pjsip_name_addr*)PJSIP_MSG_FROM_HDR(rdata->msg_info.msg)->uri);
}

/*!
 * \internal
 * \brief Adds diversion header information to an outbound SIP message
 *
 * \param tdata The outbound message
 * \param data The redirecting data used to fill parts of the diversion header
 */
static void add_diversion_header(pjsip_tx_data *tdata, struct ast_party_redirecting *data)
{
	pjsip_fromto_hdr *hdr;
	pjsip_name_addr *name_addr;
	pjsip_sip_uri *uri;
	pjsip_param *param;
	pjsip_fromto_hdr *old_hdr;
	const char *reason_str;
	const char *quote_str;
	char *reason_buf;

	struct ast_party_id *id = &data->from;
	pjsip_uri *base = PJSIP_MSG_FROM_HDR(tdata->msg)->uri;

	if (!id->number.valid || ast_strlen_zero(id->number.str)) {
		return;
	}

	hdr = pjsip_from_hdr_create(tdata->pool);
	hdr->type = PJSIP_H_OTHER;
	hdr->sname = hdr->name = diversion_name;

	name_addr = pjsip_uri_clone(tdata->pool, base);
	uri = pjsip_uri_get_uri(name_addr->uri);

	pj_strdup2(tdata->pool, &name_addr->display, id->name.str);
	pj_strdup2(tdata->pool, &uri->user, id->number.str);

	param = PJ_POOL_ALLOC_T(tdata->pool, pjsip_param);
	param->name = pj_str("reason");

	reason_str = reason_code_to_str(&data->reason);

	/* Reason is either already quoted or it is a token to not need quotes added. */
	quote_str = *reason_str == '\"' || sip_is_token(reason_str) ? "" : "\"";

	reason_buf = pj_pool_alloc(tdata->pool, strlen(reason_str) + 3);
	sprintf(reason_buf, "%s%s%s", quote_str, reason_str, quote_str);/* Safe */

	param->value = pj_str(reason_buf);

	pj_list_insert_before(&hdr->other_param, param);

	hdr->uri = (pjsip_uri *) name_addr;
	old_hdr = pjsip_msg_find_hdr_by_name(tdata->msg, &diversion_name, NULL);
	if (old_hdr) {
		pj_list_erase(old_hdr);
	}
	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *)hdr);
}

static void get_redirecting_add_diversion(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	struct ast_party_redirecting *data;

	if (session->channel && session->endpoint->id.send_diversion &&
	    (data = ast_channel_redirecting(session->channel))->count) {
		add_diversion_header(tdata, data);
	}
}

/*!
 * \internal
 * \brief Adds a diversion header to an outgoing INVITE request if
 *  redirecting information is available.
 *
 * \param session The session on which the INVITE request is to be sent
 * \param tdata The outbound INVITE request
 */
static void diversion_outgoing_request(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	get_redirecting_add_diversion(session, tdata);
}

/*!
 * \internal
 * \brief Adds a diversion header to an outgoing 3XX response
 *
 * \param session The session on which the INVITE response is to be sent
 * \param tdata The outbound INVITE response
 */
static void diversion_outgoing_response(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	struct pjsip_status_line status = tdata->msg->line.status;

	/* add to 302 and 181 */
	if (PJSIP_IS_STATUS_IN_CLASS(status.code, 300) || (status.code == 181)) {
		get_redirecting_add_diversion(session, tdata);
	}
}

static struct ast_sip_session_supplement diversion_supplement = {
	.method = "INVITE",
	/* this supplement needs to be called after caller id
           and after the channel has been created */
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL + 100,
	.incoming_request = diversion_incoming_request,
	.incoming_response = diversion_incoming_response,
	.outgoing_request = diversion_outgoing_request,
	.outgoing_response = diversion_outgoing_response,
	.response_priority = AST_SIP_SESSION_BEFORE_REDIRECTING,
};

static int load_module(void)
{
	/* Because we are passing static memory to pjsip, we need to make sure it
	 * stays valid while we potentially have active sessions */
	ast_module_shutdown_ref(ast_module_info->self);
	ast_sip_session_register_supplement(&diversion_supplement);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&diversion_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Add Diversion Header Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip,res_pjsip_session",
);
