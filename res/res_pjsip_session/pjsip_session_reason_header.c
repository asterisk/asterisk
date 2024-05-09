/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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

#include "asterisk.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/utils.h"
#include "pjsip_session.h"

static const pj_str_t reason_hdr_str = { "Reason", 6};

struct return_reason_data {
	char *protocol;
	int response_code;
	char *response_str;
	int already_sent;
};

static void return_reason_destructor(void *obj)
{
	struct return_reason_data *rr = obj;
	SCOPE_ENTER(3, "Destroying RR");
	ast_free(rr->protocol);
	ast_free(rr->response_str);
	ast_free(rr);
	SCOPE_EXIT("Done");
}

#define RETURN_REASON_DATASTORE_NAME "pjsip_session_return_reason"
static struct ast_datastore_info return_reason_info = {
	.type = RETURN_REASON_DATASTORE_NAME,
	.destroy = return_reason_destructor,
};

static void reason_header_outgoing_response(struct ast_sip_session *session,
	struct pjsip_tx_data *tdata)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
	pjsip_generic_string_hdr *reason_hdr;
	pj_str_t reason_val;
	RAII_VAR(char *, reason_str, NULL, ast_free);
	struct return_reason_data *rr = NULL;
	int rc = 0;
	struct pjsip_status_line status = tdata->msg->line.status;
	const char *tag = ast_sip_session_get_name(session);
	SCOPE_ENTER(3, "%s: Response Code: %d\n", tag,
		status.code);

	/*
	 * Include the Reason header if this is a provisional
	 * response other than a 100 OR it's a 200.
	 */
	if (!((PJSIP_IS_STATUS_IN_CLASS(status.code, 100) && status.code != 100) || status.code == 200)) {
		SCOPE_EXIT_RTN("%s: RC %d not eligible for Reason header\n", tag, status.code);
	}

	datastore = ast_sip_session_get_datastore(session, RETURN_REASON_DATASTORE_NAME);
	if (!datastore) {
		SCOPE_EXIT_RTN("%s: No datastore on session.  Nothing to do\n", tag);
	}
	rr = datastore->data;

	rc = ast_asprintf(&reason_str, "%s; cause=%d; text=\"%s\"",
		rr->protocol, rr->response_code, rr->response_str);
	if (rc < 0) {
		ast_sip_session_remove_datastore(session, RETURN_REASON_DATASTORE_NAME);
		SCOPE_EXIT_RTN("%s: Failed to create reason string\n", tag);
	}
	reason_val = pj_str(reason_str);

	/*
	 * pjproject re-uses the tdata for a transaction so if we've
	 * already sent the Reason header, it'll get sent again unless
	 * we remove it.  It's possible something else is sending a Reason
	 * header so we need to ensure we only remove our own.
	 */
	if (rr->already_sent) {
		ast_trace(3, "%s: Reason already sent\n", tag);
		reason_hdr = pjsip_msg_find_hdr_by_name(tdata->msg, &reason_hdr_str, NULL);
		while (reason_hdr) {
			ast_trace(3, "%s: Checking old reason: <" PJSTR_PRINTF_SPEC "> - <" PJSTR_PRINTF_SPEC "> \n",
				tag,
				PJSTR_PRINTF_VAR(reason_hdr->hvalue), PJSTR_PRINTF_VAR(reason_val));
			if (pj_strcmp(&reason_hdr->hvalue, &reason_val) == 0) {
				ast_trace(3, "%s: MATCH. Cleaning up old reason\n", tag);
				pj_list_erase(reason_hdr);
				break;
			}
			reason_hdr = pjsip_msg_find_hdr_by_name(tdata->msg, &reason_hdr_str, reason_hdr->next);
		}
		ast_sip_session_remove_datastore(session, RETURN_REASON_DATASTORE_NAME);
		SCOPE_EXIT_RTN("%s: Done\n", tag);
	}

	reason_hdr = pjsip_generic_string_hdr_create(tdata->pool, &reason_hdr_str, &reason_val);
	if (reason_hdr) {
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *)reason_hdr);
		rr->already_sent = 1;
		ast_trace(1, "%s: Created reason header: Reason: %s\n",
			tag, reason_str);
	} else {
		ast_trace(1, "%s: Failed to create reason header: Reason: %s\n",
			tag, reason_str);
	}

	SCOPE_EXIT_RTN("%s: Done\n", tag);
}

int ast_sip_session_add_reason_header(struct ast_sip_session *session,
	const char *protocol, int code, const char *text)
{
	struct return_reason_data *rr;
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
	const char *tag = ast_sip_session_get_name(session);
	SCOPE_ENTER(4, "%s: Adding Reason header %s %d %s\n",
		tag, S_OR(protocol,"<missing protocol>"),
		code, S_OR(text, "<missing text>"));

	if (ast_strlen_zero(protocol) || !text) {
		SCOPE_EXIT_RTN_VALUE(-1, "%s: Missing protocol or text\n", tag);
	}
	rr = ast_calloc(1, sizeof(*rr));
	if (!rr) {
		SCOPE_EXIT_RTN_VALUE(-1, "%s: Failed to allocate datastore\n", tag);
	}
	datastore =	ast_sip_session_alloc_datastore(
		&return_reason_info, return_reason_info.type);
	rr->protocol = ast_strdup(protocol);
	rr->response_code = code;
	rr->response_str = ast_strdup(text);
	datastore->data = rr;
	if (ast_sip_session_add_datastore(session, datastore) != 0) {
		SCOPE_EXIT_RTN_VALUE(-1,
			"%s: Failed to add datastore to session\n", tag);
	}

	SCOPE_EXIT_RTN_VALUE(0, "%s: Done\n", tag);
}

static struct ast_sip_session_supplement reason_header_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL + 1, /* Run AFTER channel creation */
	.outgoing_response = reason_header_outgoing_response,
};

void pjsip_reason_header_unload(void)
{
	ast_sip_session_unregister_supplement(&reason_header_supplement);
}

void pjsip_reason_header_load(void)
{
	ast_sip_session_register_supplement(&reason_header_supplement);
}
