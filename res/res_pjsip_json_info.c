/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Digium, Inc.
 *
 * Bradley Latus <brad.latus@gmail.com>
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
#include "asterisk/module.h"

static void send_response(struct ast_sip_session *session,
		struct pjsip_rx_data *rdata, int code)
{
	pjsip_tx_data *tdata;
	pjsip_dialog *dlg = session->inv_session->dlg;

	if (pjsip_dlg_create_response(dlg, rdata, code, NULL, &tdata) == PJ_SUCCESS) {
		struct pjsip_transaction *tsx = pjsip_rdata_get_tsx(rdata);
		pjsip_dlg_send_response(dlg, tsx, tdata);
	}
}

static int is_json_type(pjsip_rx_data *rdata, char *subtype)
{
	return rdata->msg_info.ctype
		&& !pj_strcmp2(&rdata->msg_info.ctype->media.type, "application")
		&& !pj_strcmp2(&rdata->msg_info.ctype->media.subtype, subtype);
}

static int json_info_incoming_request(struct ast_sip_session *session,
		struct pjsip_rx_data *rdata)
{

	if (!is_json_type(rdata, "json")) {
		/* Let another module respond */
		return 0;
	}

	ast_log(LOG_NOTICE, "Received SIP INFO from channel %s\n", ast_channel_name(session->channel));

	/* Need to return 200 OK */
	send_response(session, rdata, 200);
	return 1;

}

static struct ast_sip_session_supplement json_info_supplement = {
	.method = "INFO",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_FIRST,
	.incoming_request = json_info_incoming_request,
};

static int load_module(void)
{
	ast_sip_session_register_supplement(&json_info_supplement);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&json_info_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP JSON INFO Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip,res_pjsip_session",
);


	// if (!strcmp(event, "presence") || !strcmp(event, "dialog")) { /* Presence, RFC 3842 */
	// 	int gotdest;
	// 	const char *accept;
	// 	int start = 0;
	// 	enum subscriptiontype subscribed = NONE;
	// 	const char *unknown_accept = NULL;

    //             /* Get destination right away */
    //             gotdest = get_destination(p, NULL, NULL);
	// 	if (gotdest != SIP_GET_DEST_EXTEN_FOUND) {
	// 		if (gotdest == SIP_GET_DEST_INVALID_URI) {
	// 			transmit_response(p, "416 Unsupported URI scheme", req);
	// 		} else {
	// 			transmit_response(p, "404 Not Found", req);
	// 		}
	// 		pvt_set_needdestroy(p, "subscription target not found");
	// 		if (authpeer) {
	// 			sip_unref_peer(authpeer, "sip_unref_peer, from handle_request_subscribe (authpeer 2)");
	// 		}
	// 		return 0;
	// 	}

	// 	/* Header from Xten Eye-beam Accept: multipart/related, application/rlmi+xml, application/pidf+xml, application/xpidf+xml */
	// 	accept = __get_header(req, "Accept", &start);
	// 	while ((subscribed == NONE) && !ast_strlen_zero(accept)) {
	// 		if (strstr(accept, "application/pidf+xml")) {
	// 			if (strstr(p->useragent, "Polycom")) {
	// 				subscribed = XPIDF_XML; /* Older versions of Polycom firmware will claim pidf+xml, but really they only support xpidf+xml */
	// 			} else {
	// 				subscribed = PIDF_XML; /* RFC 3863 format */
	// 			}
	// 		} else if (strstr(accept, "application/dialog-info+xml")) {
	// 			subscribed = DIALOG_INFO_XML;
	// 			/* IETF draft: draft-ietf-sipping-dialog-package-05.txt */
	// 		} else if (strstr(accept, "application/cpim-pidf+xml")) {
	// 			subscribed = CPIM_PIDF_XML;    /* RFC 3863 format */
	// 		} else if (strstr(accept, "application/xpidf+xml")) {
	// 			subscribed = XPIDF_XML;        /* Early pre-RFC 3863 format with MSN additions (Microsoft Messenger) */
	// 		} else {
	// 			unknown_accept = accept;
	// 		}
	// 		/* check to see if there is another Accept header present */
	// 		accept = __get_header(req, "Accept", &start);
	// 	}

	// 	if (!start) {
	// 		if (p->subscribed == NONE) { /* if the subscribed field is not already set, and there is no accept header... */
	// 			transmit_response(p, "489 Bad Event", req);
	// 			ast_log(LOG_WARNING,"SUBSCRIBE failure: no Accept header: pvt: "
	// 				"stateid: %d, laststate: %d, dialogver: %u, subscribecont: "
	// 				"'%s', subscribeuri: '%s'\n",
	// 				p->stateid,
	// 				p->laststate,
	// 				p->dialogver,
	// 				p->subscribecontext,
	// 				p->subscribeuri);
	// 			pvt_set_needdestroy(p, "no Accept header");
	// 			if (authpeer) {
	// 				sip_unref_peer(authpeer, "sip_unref_peer, from handle_request_subscribe (authpeer 2)");
	// 			}
	// 			return 0;
	// 		}
	// 		/* if p->subscribed is non-zero, then accept is not obligatory; according to rfc 3265 section 3.1.3, at least.
	// 		   so, we'll just let it ride, keeping the value from a previous subscription, and not abort the subscription */
	// 	} else if (subscribed == NONE) {
	// 		/* Can't find a format for events that we know about */
	// 		char buf[200];

	// 		if (!ast_strlen_zero(unknown_accept)) {
	// 			snprintf(buf, sizeof(buf), "489 Bad Event (format %s)", unknown_accept);
	// 		} else {
	// 			snprintf(buf, sizeof(buf), "489 Bad Event");
	// 		}
	// 		transmit_response(p, buf, req);
	// 		ast_log(LOG_WARNING,"SUBSCRIBE failure: unrecognized format:"
	// 			"'%s' pvt: subscribed: %d, stateid: %d, laststate: %d,"
	// 			"dialogver: %u, subscribecont: '%s', subscribeuri: '%s'\n",
	// 			unknown_accept,
	// 			(int)p->subscribed,
	// 			p->stateid,
	// 			p->laststate,
	// 			p->dialogver,
	// 			p->subscribecontext,
	// 			p->subscribeuri);
	// 		pvt_set_needdestroy(p, "unrecognized format");
	// 		if (authpeer) {
	// 			sip_unref_peer(authpeer, "sip_unref_peer, from handle_request_subscribe (authpeer 2)");
	// 		}
	// 		return 0;
	// 	} else {
	// 		p->subscribed = subscribed;
	// 	}
	// } else if (!strcmp(event, "message-summary")) {
	// 	int start = 0;
	// 	int found_supported = 0;
	// 	const char *accept;

	// 	accept = __get_header(req, "Accept", &start);
	// 	while (!found_supported && !ast_strlen_zero(accept)) {
	// 		found_supported = strcmp(accept, "application/simple-message-summary") ? 0 : 1;
	// 		if (!found_supported) {
	// 			ast_debug(3, "Received SIP mailbox subscription for unknown format: %s\n", accept);
	// 		}
	// 		accept = __get_header(req, "Accept", &start);
	// 	}
	// 	/* If !start, there is no Accept header at all */
	// 	if (start && !found_supported) {
	// 		/* Format requested that we do not support */
	// 		transmit_response(p, "406 Not Acceptable", req);
	// 		ast_debug(2, "Received SIP mailbox subscription for unknown format: %s\n", accept);
	// 		pvt_set_needdestroy(p, "unknown format");
	// 		if (authpeer) {
	// 			sip_unref_peer(authpeer, "sip_unref_peer, from handle_request_subscribe (authpeer 3)");
	// 		}
	// 		return 0;
	// 	}
	// 	/* Looks like they actually want a mailbox status
	// 	  This version of Asterisk supports mailbox subscriptions
	// 	  The subscribed URI needs to exist in the dial plan
	// 	  In most devices, this is configurable to the voicemailmain extension you use
	// 	*/
	// 	if (!authpeer || AST_LIST_EMPTY(&authpeer->mailboxes)) {
	// 		if (!authpeer) {
	// 			transmit_response(p, "404 Not found", req);
	// 		} else {
	// 			transmit_response(p, "404 Not found (no mailbox)", req);
	// 			ast_log(LOG_NOTICE, "Received SIP subscribe for peer without mailbox: %s\n", S_OR(authpeer->name, ""));
	// 		}
	// 		pvt_set_needdestroy(p, "received 404 response");

	// 		if (authpeer) {
	// 			sip_unref_peer(authpeer, "sip_unref_peer, from handle_request_subscribe (authpeer 3)");
	// 		}
	// 		return 0;
	// 	}

	// 	p->subscribed = MWI_NOTIFICATION;
	// 	if (ast_test_flag(&authpeer->flags[1], SIP_PAGE2_SUBSCRIBEMWIONLY)) {
	// 		ao2_unlock(p);
	// 		add_peer_mwi_subs(authpeer);
	// 		ao2_lock(p);
	// 	}
	// 	if (authpeer->mwipvt != p) {	/* Destroy old PVT if this is a new one */
	// 		/* We only allow one subscription per peer */
	// 		if (authpeer->mwipvt) {
	// 			dialog_unlink_all(authpeer->mwipvt);
	// 			authpeer->mwipvt = dialog_unref(authpeer->mwipvt, "unref dialog authpeer->mwipvt");
	// 		}
	// 		authpeer->mwipvt = dialog_ref(p, "setting peers' mwipvt to p");
	// 	}

	// 	if (p->relatedpeer != authpeer) {
	// 		if (p->relatedpeer) {
	// 			sip_unref_peer(p->relatedpeer, "Unref previously stored relatedpeer ptr");
	// 		}
	// 		p->relatedpeer = sip_ref_peer(authpeer, "setting dialog's relatedpeer pointer");
	// 	}
	// 	/* Do not release authpeer here */
	// } else if (!strcmp(event, "call-completion")) {
	// 	handle_cc_subscribe(p, req);
	// } else { /* At this point, Asterisk does not understand the specified event */
	// 	transmit_response(p, "489 Bad Event", req);
	// 	ast_debug(2, "Received SIP subscribe for unknown event package: %s\n", event);
	// 	pvt_set_needdestroy(p, "unknown event package");
	// 	if (authpeer) {
	// 		sip_unref_peer(authpeer, "sip_unref_peer, from handle_request_subscribe (authpeer 5)");
	// 	}
	// 	return 0;
	// }