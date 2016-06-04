/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
	<depend>res_pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "asterisk/paths.h"
#include "asterisk/test.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/manager.h"
#include "asterisk/named_locks.h"
#include "asterisk/res_pjproject.h"
#include "res_pjsip/include/res_pjsip_private.h"

/*** DOCUMENTATION
	<manager name="PJSIPShowRegistrationsInbound" language="en_US">
		<synopsis>
			Lists PJSIP inbound registrations.
		</synopsis>
		<syntax />
		<description>
			<para>
			In response <literal>InboundRegistrationDetail</literal> events showing configuration and status
			information are raised for each inbound registration object.  As well as <literal>AuthDetail</literal>
			events for each associated auth object.  Once all events are completed an
			<literal>InboundRegistrationDetailComplete</literal> is issued.
                        </para>
		</description>
	</manager>
 ***/

static int pj_max_hostname = PJ_MAX_HOSTNAME;
static int pjsip_max_url_size = PJSIP_MAX_URL_SIZE;

/*! \brief Internal function which returns the expiration time for a contact */
static int registrar_get_expiration(const struct ast_sip_aor *aor, const pjsip_contact_hdr *contact, const pjsip_rx_data *rdata)
{
	pjsip_expires_hdr *expires;
	int expiration = aor->default_expiration;

	if (contact && contact->expires != -1) {
		/* Expiration was provided with the contact itself */
		expiration = contact->expires;
	} else if ((expires = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_EXPIRES, NULL))) {
		/* Expiration was provided using the Expires header */
		expiration = expires->ivalue;
	}

	/* If the value has explicitly been set to 0, do not enforce */
	if (!expiration) {
		return expiration;
	}

	/* Enforce the range that we will allow for expiration */
	if (expiration < aor->minimum_expiration) {
		expiration = aor->minimum_expiration;
	} else if (expiration > aor->maximum_expiration) {
		expiration = aor->maximum_expiration;
	}

	return expiration;
}

/*! \brief Structure used for finding contact */
struct registrar_contact_details {
	/*! \brief Pool used for parsing URI */
	pj_pool_t *pool;
	/*! \brief URI being looked for */
	pjsip_sip_uri *uri;
};

/*! \brief Callback function for finding a contact */
static int registrar_find_contact(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	const struct registrar_contact_details *details = arg;
	pjsip_uri *contact_uri = pjsip_parse_uri(details->pool, (char*)contact->uri, strlen(contact->uri), 0);

	return (pjsip_uri_cmp(PJSIP_URI_IN_CONTACT_HDR, details->uri, contact_uri) == PJ_SUCCESS) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Internal function which validates provided Contact headers to confirm that they are acceptable, and returns number of contacts */
static int registrar_validate_contacts(const pjsip_rx_data *rdata, struct ao2_container *contacts, struct ast_sip_aor *aor, int *added, int *updated, int *deleted)
{
	pjsip_contact_hdr *previous = NULL, *contact = (pjsip_contact_hdr *)&rdata->msg_info.msg->hdr;
	struct registrar_contact_details details = {
		.pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "Contact Comparison", 256, 256),
	};

	if (!details.pool) {
		return -1;
	}

	while ((contact = (pjsip_contact_hdr *) pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, contact->next))) {
		int expiration = registrar_get_expiration(aor, contact, rdata);
		RAII_VAR(struct ast_sip_contact *, existing, NULL, ao2_cleanup);
		char contact_uri[pjsip_max_url_size];

		if (contact->star) {
			/* The expiration MUST be 0 when a '*' contact is used and there must be no other contact */
			if ((expiration != 0) || previous) {
				pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), details.pool);
				return -1;
			}
			continue;
		} else if (previous && previous->star) {
			/* If there is a previous contact and it is a '*' this is a deal breaker */
			pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), details.pool);
			return -1;
		}
		previous = contact;

		if (!PJSIP_URI_SCHEME_IS_SIP(contact->uri) && !PJSIP_URI_SCHEME_IS_SIPS(contact->uri)) {
			continue;
		}

		details.uri = pjsip_uri_get_uri(contact->uri);

		/* pjsip_uri_print returns -1 if there's not enough room in the buffer */
		if (pjsip_uri_print(PJSIP_URI_IN_CONTACT_HDR, details.uri, contact_uri, sizeof(contact_uri)) < 0) {
			/* If the total length of the uri is greater than pjproject can handle, go no further */
			pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), details.pool);
			return -1;
		}

		if (details.uri->host.slen >= pj_max_hostname) {
			/* If the length of the hostname is greater than pjproject can handle, go no further */
			pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), details.pool);
			return -1;
		}

		/* Determine if this is an add, update, or delete for policy enforcement purposes */
		if (!(existing = ao2_callback(contacts, 0, registrar_find_contact, &details))) {
			if (expiration) {
				(*added)++;
			}
		} else if (expiration) {
			(*updated)++;
		} else {
			(*deleted)++;
		}
	}

	/* The provided contacts are acceptable, huzzah! */
	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), details.pool);
	return 0;
}

/*! \brief Callback function which prunes static contacts */
static int registrar_prune_static(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;

	return ast_tvzero(contact->expiration_time) ? CMP_MATCH : 0;
}

/*! \brief Internal function used to delete a contact from an AOR */
static int registrar_delete_contact(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	const char *aor_name = arg;

	ast_sip_location_delete_contact(contact);
	if (!ast_strlen_zero(aor_name)) {
		ast_verb(3, "Removed contact '%s' from AOR '%s' due to request\n", contact->uri, aor_name);
		ast_test_suite_event_notify("AOR_CONTACT_REMOVED",
				"Contact: %s\r\n"
				"AOR: %s\r\n"
				"UserAgent: %s",
				contact->uri,
				aor_name,
				contact->user_agent);
	}

	return 0;
}

/*! \brief Internal function which adds a contact to a response */
static int registrar_add_contact(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	pjsip_tx_data *tdata = arg;
	pjsip_contact_hdr *hdr = pjsip_contact_hdr_create(tdata->pool);
	pj_str_t uri;

	pj_strdup2_with_null(tdata->pool, &uri, contact->uri);
	hdr->uri = pjsip_parse_uri(tdata->pool, uri.ptr, uri.slen, PJSIP_PARSE_URI_AS_NAMEADDR);
	hdr->expires = ast_tvdiff_ms(contact->expiration_time, ast_tvnow()) / 1000;

	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)hdr);

	return 0;
}

/*! \brief Helper function which adds a Date header to a response */
static void registrar_add_date_header(pjsip_tx_data *tdata)
{
	char date[256];
	struct tm tm;
	time_t t = time(NULL);

	gmtime_r(&t, &tm);
	strftime(date, sizeof(date), "%a, %d %b %Y %T GMT", &tm);

	ast_sip_add_header(tdata, "Date", date);
}

static const pj_str_t path_hdr_name = { "Path", 4 };

static int build_path_data(pjsip_rx_data *rdata, struct ast_str **path_str)
{
	pjsip_generic_string_hdr *path_hdr = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &path_hdr_name, NULL);

	if (!path_hdr) {
		return 0;
	}

	*path_str = ast_str_create(64);
	if (!path_str) {
		return -1;
	}

	ast_str_set(path_str, 0, "%.*s", (int)path_hdr->hvalue.slen, path_hdr->hvalue.ptr);

	while ((path_hdr = (pjsip_generic_string_hdr *) pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &path_hdr_name, path_hdr->next))) {
		ast_str_append(path_str, 0, ",%.*s", (int)path_hdr->hvalue.slen, path_hdr->hvalue.ptr);
	}

	return 0;
}

static int registrar_validate_path(pjsip_rx_data *rdata, struct ast_sip_aor *aor, struct ast_str **path_str)
{
	const pj_str_t path_supported_name = { "path", 4 };
	pjsip_supported_hdr *supported_hdr;
	int i;

	if (!aor->support_path) {
		return 0;
	}

	if (build_path_data(rdata, path_str)) {
		return -1;
	}

	if (!*path_str) {
		return 0;
	}

	supported_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_SUPPORTED, NULL);
	if (!supported_hdr) {
		return -1;
	}

	/* Find advertised path support */
	for (i = 0; i < supported_hdr->count; i++) {
		if (!pj_stricmp(&supported_hdr->values[i], &path_supported_name)) {
			return 0;
		}
	}

	/* Path header present, but support not advertised */
	return -1;
}

static int register_aor_core(pjsip_rx_data *rdata,
	struct ast_sip_endpoint *endpoint,
	struct ast_sip_aor *aor,
	const char *aor_name,
	struct ao2_container *contacts)
{
	static const pj_str_t USER_AGENT = { "User-Agent", 10 };

	int added = 0, updated = 0, deleted = 0;
	pjsip_contact_hdr *contact_hdr = NULL;
	struct registrar_contact_details details = { 0, };
	pjsip_tx_data *tdata;
	RAII_VAR(struct ast_str *, path_str, NULL, ast_free);
	struct ast_sip_contact *response_contact;
	char *user_agent = NULL;
	pjsip_user_agent_hdr *user_agent_hdr;
	pjsip_expires_hdr *expires_hdr;
	pjsip_via_hdr *via_hdr;
	pjsip_via_hdr *via_hdr_last;
	char *via_addr = NULL;
	int via_port = 0;
	pjsip_cid_hdr *call_id_hdr;
	char *call_id = NULL;
	size_t alloc_size;

	/* So we don't count static contacts against max_contacts we prune them out from the container */
	ao2_callback(contacts, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE, registrar_prune_static, NULL);

	if (registrar_validate_contacts(rdata, contacts, aor, &added, &updated, &deleted)) {
		/* The provided Contact headers do not conform to the specification */
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 400, NULL, NULL, NULL);
		ast_sip_report_failed_acl(endpoint, rdata, "registrar_invalid_contacts_provided");
		ast_log(LOG_WARNING, "Failed to validate contacts in REGISTER request from '%s'\n",
				ast_sorcery_object_get_id(endpoint));
		return PJ_TRUE;
	}

	if (registrar_validate_path(rdata, aor, &path_str)) {
		/* Ensure that intervening proxies did not make invalid modifications to the request */
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 420, NULL, NULL, NULL);
		ast_log(LOG_WARNING, "Invalid modifications made to REGISTER request from '%s' by intervening proxy\n",
				ast_sorcery_object_get_id(endpoint));
		return PJ_TRUE;
	}

	if ((MAX(added - deleted, 0) + (!aor->remove_existing ? ao2_container_count(contacts) : 0)) > aor->max_contacts) {
		/* Enforce the maximum number of contacts */
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 403, NULL, NULL, NULL);
		ast_sip_report_failed_acl(endpoint, rdata, "registrar_attempt_exceeds_maximum_configured_contacts");
		ast_log(LOG_WARNING, "Registration attempt from endpoint '%s' to AOR '%s' will exceed max contacts of %u\n",
				ast_sorcery_object_get_id(endpoint), aor_name, aor->max_contacts);
		return PJ_TRUE;
	}

	if (!(details.pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "Contact Comparison", 256, 256))) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	user_agent_hdr = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &USER_AGENT, NULL);
	if (user_agent_hdr) {
		alloc_size = pj_strlen(&user_agent_hdr->hvalue) + 1;
		user_agent = ast_alloca(alloc_size);
		ast_copy_pj_str(user_agent, &user_agent_hdr->hvalue, alloc_size);
	}

	/* Find the first Via header */
	via_hdr = via_hdr_last = (pjsip_via_hdr*) pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_VIA, NULL);
	if (via_hdr) {
		/* Find the last Via header */
		while ( (via_hdr = (pjsip_via_hdr*) pjsip_msg_find_hdr(rdata->msg_info.msg,
				PJSIP_H_VIA, via_hdr->next)) != NULL) {
			via_hdr_last = via_hdr;
		}
		alloc_size = pj_strlen(&via_hdr_last->sent_by.host) + 1;
		via_addr = ast_alloca(alloc_size);
		ast_copy_pj_str(via_addr, &via_hdr_last->sent_by.host, alloc_size);
		via_port=via_hdr_last->sent_by.port;
	}

	call_id_hdr = (pjsip_cid_hdr*) pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CALL_ID, NULL);
	if (call_id_hdr) {
		alloc_size = pj_strlen(&call_id_hdr->id) + 1;
		call_id = ast_alloca(alloc_size);
		ast_copy_pj_str(call_id, &call_id_hdr->id, alloc_size);
	}

	/* Iterate each provided Contact header and add, update, or delete */
	while ((contact_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, contact_hdr ? contact_hdr->next : NULL))) {
		int expiration;
		char contact_uri[pjsip_max_url_size];
		RAII_VAR(struct ast_sip_contact *, contact, NULL, ao2_cleanup);

		if (contact_hdr->star) {
			/* A star means to unregister everything, so do so for the possible contacts */
			ao2_callback(contacts, OBJ_NODATA | OBJ_MULTIPLE, registrar_delete_contact, (void *)aor_name);
			break;
		}

		if (!PJSIP_URI_SCHEME_IS_SIP(contact_hdr->uri) && !PJSIP_URI_SCHEME_IS_SIPS(contact_hdr->uri)) {
			/* This registrar only currently supports sip: and sips: URI schemes */
			continue;
		}

		expiration = registrar_get_expiration(aor, contact_hdr, rdata);
		details.uri = pjsip_uri_get_uri(contact_hdr->uri);
		pjsip_uri_print(PJSIP_URI_IN_CONTACT_HDR, details.uri, contact_uri, sizeof(contact_uri));

		if (!(contact = ao2_callback(contacts, OBJ_UNLINK, registrar_find_contact, &details))) {
			/* If they are actually trying to delete a contact that does not exist... be forgiving */
			if (!expiration) {
				ast_verb(3, "Attempted to remove non-existent contact '%s' from AOR '%s' by request\n",
					contact_uri, aor_name);
				continue;
			}

			if (ast_sip_location_add_contact_nolock(aor, contact_uri, ast_tvadd(ast_tvnow(),
				ast_samp2tv(expiration, 1)), path_str ? ast_str_buffer(path_str) : NULL,
					user_agent, via_addr, via_port, call_id, endpoint)) {
				ast_log(LOG_ERROR, "Unable to bind contact '%s' to AOR '%s'\n",
						contact_uri, aor_name);
				continue;
			}

			ast_verb(3, "Added contact '%s' to AOR '%s' with expiration of %d seconds\n",
				contact_uri, aor_name, expiration);
			ast_test_suite_event_notify("AOR_CONTACT_ADDED",
					"Contact: %s\r\n"
					"AOR: %s\r\n"
					"Expiration: %d\r\n"
					"UserAgent: %s",
					contact_uri,
					aor_name,
					expiration,
					user_agent);
		} else if (expiration) {
			struct ast_sip_contact *contact_update;

			contact_update = ast_sorcery_copy(ast_sip_get_sorcery(), contact);
			if (!contact_update) {
				ast_log(LOG_ERROR, "Failed to update contact '%s' expiration time to %d seconds.\n",
					contact->uri, expiration);
				continue;
			}

			contact_update->expiration_time = ast_tvadd(ast_tvnow(), ast_samp2tv(expiration, 1));
			contact_update->qualify_frequency = aor->qualify_frequency;
			contact_update->authenticate_qualify = aor->authenticate_qualify;
			if (path_str) {
				ast_string_field_set(contact_update, path, ast_str_buffer(path_str));
			}
			if (user_agent) {
				ast_string_field_set(contact_update, user_agent, user_agent);
			}
			if (!ast_strlen_zero(ast_config_AST_SYSTEM_NAME)) {
				ast_string_field_set(contact_update, reg_server, ast_config_AST_SYSTEM_NAME);
			}

			if (ast_sip_location_update_contact(contact_update)) {
				ast_log(LOG_ERROR, "Failed to update contact '%s' expiration time to %d seconds.\n",
					contact->uri, expiration);
				ast_sip_location_delete_contact(contact);
				continue;
			}
			ast_debug(3, "Refreshed contact '%s' on AOR '%s' with new expiration of %d seconds\n",
				contact_uri, aor_name, expiration);
			ast_test_suite_event_notify("AOR_CONTACT_REFRESHED",
					"Contact: %s\r\n"
					"AOR: %s\r\n"
					"Expiration: %d\r\n"
					"UserAgent: %s",
					contact_uri,
					aor_name,
					expiration,
					contact_update->user_agent);
			ao2_cleanup(contact_update);
		} else {
			/* We want to report the user agent that was actually in the removed contact */
			ast_sip_location_delete_contact(contact);
			ast_verb(3, "Removed contact '%s' from AOR '%s' due to request\n", contact_uri, aor_name);
			ast_test_suite_event_notify("AOR_CONTACT_REMOVED",
					"Contact: %s\r\n"
					"AOR: %s\r\n"
					"UserAgent: %s",
					contact_uri,
					aor_name,
					contact->user_agent);
		}
	}

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), details.pool);

	/* If the AOR is configured to remove any existing contacts that have not been updated/added as a result of this REGISTER
	 * do so
	 */
	if (aor->remove_existing) {
		ao2_callback(contacts, OBJ_NODATA | OBJ_MULTIPLE, registrar_delete_contact, NULL);
	}

	/* Re-retrieve contacts.  Caller will clean up the original container. */
	contacts = ast_sip_location_retrieve_aor_contacts_nolock(aor);
	response_contact = ao2_callback(contacts, 0, NULL, NULL);

	/* Send a response containing all of the contacts (including static) that are present on this AOR */
	if (ast_sip_create_response(rdata, 200, response_contact, &tdata) != PJ_SUCCESS) {
		ao2_cleanup(response_contact);
		ao2_cleanup(contacts);
		return PJ_TRUE;
	}
	ao2_cleanup(response_contact);

	/* Add the date header to the response, some UAs use this to set their date and time */
	registrar_add_date_header(tdata);

	ao2_callback(contacts, 0, registrar_add_contact, tdata);
	ao2_cleanup(contacts);

	if ((expires_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_EXPIRES, NULL))) {
		expires_hdr = pjsip_expires_hdr_create(tdata->pool, registrar_get_expiration(aor, NULL, rdata));
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)expires_hdr);
	}

	ast_sip_send_stateful_response(rdata, tdata, endpoint);

	return PJ_TRUE;
}

static int register_aor(pjsip_rx_data *rdata,
	struct ast_sip_endpoint *endpoint,
	struct ast_sip_aor *aor,
	const char *aor_name)
{
	int res;
	struct ao2_container *contacts = NULL;
	struct ast_named_lock *lock;

	lock = ast_named_lock_get(AST_NAMED_LOCK_TYPE_RWLOCK, "aor", aor_name);
	if (!lock) {
		return PJ_TRUE;
	}

	ao2_wrlock(lock);
	contacts = ast_sip_location_retrieve_aor_contacts_nolock(aor);
	if (!contacts) {
		ao2_unlock(lock);
		ast_named_lock_put(lock);
		return PJ_TRUE;
	}

	res = register_aor_core(rdata, endpoint, aor, aor_name, contacts);
	ao2_cleanup(contacts);
	ao2_unlock(lock);
	ast_named_lock_put(lock);

	return res;
}

static int match_aor(const char *aor_name, const char *id)
{
	if (ast_strlen_zero(aor_name)) {
		return 0;
	}

	if (!strcmp(aor_name, id)) {
		ast_debug(3, "Matched id '%s' to aor '%s'\n", id, aor_name);
		return 1;
	}

	return 0;
}

static char *find_aor_name(const char *username, const char *domain, const char *aors)
{
	char *configured_aors;
	char *aor_name;
	char *id_domain;
	struct ast_sip_domain_alias *alias;

	id_domain = ast_alloca(strlen(username) + strlen(domain) + 2);
	sprintf(id_domain, "%s@%s", username, domain);

	/* Look for exact match on username@domain */
	configured_aors = ast_strdupa(aors);
	while ((aor_name = ast_strip(strsep(&configured_aors, ",")))) {
		if (match_aor(aor_name, id_domain)) {
			return ast_strdup(aor_name);
		}
	}

	/* If there's a domain alias, look for exact match on username@domain_alias */
	alias = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "domain_alias", domain);
	if (alias) {
		char *id_domain_alias = ast_alloca(strlen(username) + strlen(alias->domain) + 2);

		sprintf(id_domain, "%s@%s", username, alias->domain);
		ao2_cleanup(alias);

		configured_aors = ast_strdupa(aors);
		while ((aor_name = ast_strip(strsep(&configured_aors, ",")))) {
			if (match_aor(aor_name, id_domain_alias)) {
				return ast_strdup(aor_name);
			}
		}
	}

	/* Look for exact match on username only */
	configured_aors = ast_strdupa(aors);
	while ((aor_name = ast_strip(strsep(&configured_aors, ",")))) {
		if (match_aor(aor_name, username)) {
			return ast_strdup(aor_name);
		}
	}

	return NULL;
}

static struct ast_sip_aor *find_registrar_aor(struct pjsip_rx_data *rdata, struct ast_sip_endpoint *endpoint)
{
	struct ast_sip_aor *aor = NULL;
	char *aor_name = NULL;
	char *domain_name;
	char *username = NULL;
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&endpoint->ident_method_order); ++i) {
		pjsip_sip_uri *uri;
		pjsip_authorization_hdr *header = NULL;

		switch (AST_VECTOR_GET(&endpoint->ident_method_order, i)) {
		case AST_SIP_ENDPOINT_IDENTIFY_BY_USERNAME:
			uri = pjsip_uri_get_uri(rdata->msg_info.to->uri);

			domain_name = ast_alloca(uri->host.slen + 1);
			ast_copy_pj_str(domain_name, &uri->host, uri->host.slen + 1);
			username = ast_alloca(uri->user.slen + 1);
			ast_copy_pj_str(username, &uri->user, uri->user.slen + 1);

			aor_name = find_aor_name(username, domain_name, endpoint->aors);
			if (aor_name) {
				ast_debug(3, "Matched aor '%s' by To username\n", aor_name);
			}
			break;
		case AST_SIP_ENDPOINT_IDENTIFY_BY_AUTH_USERNAME:
			while ((header = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_AUTHORIZATION,
				header ? header->next : NULL))) {
				if (header && !pj_stricmp2(&header->scheme, "digest")) {
					username = ast_alloca(header->credential.digest.username.slen + 1);
					ast_copy_pj_str(username, &header->credential.digest.username, header->credential.digest.username.slen + 1);
					domain_name = ast_alloca(header->credential.digest.realm.slen + 1);
					ast_copy_pj_str(domain_name, &header->credential.digest.realm, header->credential.digest.realm.slen + 1);

					aor_name = find_aor_name(username, domain_name, endpoint->aors);
					if (aor_name) {
						ast_debug(3, "Matched aor '%s' by Authentication username\n", aor_name);
						break;
					}
				}
			}
			break;
		default:
			continue;
		}

		if (aor_name) {
			break;
		}
	}

	if (ast_strlen_zero(aor_name) || !(aor = ast_sip_location_retrieve_aor(aor_name))) {
		/* The provided AOR name was not found (be it within the configuration or sorcery itself) */
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 404, NULL, NULL, NULL);
		ast_sip_report_req_no_support(endpoint, rdata, "registrar_requested_aor_not_found");
		ast_log(LOG_WARNING, "AOR '%s' not found for endpoint '%s'\n",
			username ?: "", ast_sorcery_object_get_id(endpoint));
	}
	ast_free(aor_name);
	return aor;
}

static pj_bool_t registrar_on_rx_request(struct pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint,
		 ast_pjsip_rdata_get_endpoint(rdata), ao2_cleanup);
	struct ast_sip_aor *aor;
	const char *aor_name;

	if (pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_register_method) || !endpoint) {
		return PJ_FALSE;
	}

	if (ast_strlen_zero(endpoint->aors)) {
		/* Short circuit early if the endpoint has no AORs configured on it, which means no registration possible */
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 403, NULL, NULL, NULL);
		ast_sip_report_failed_acl(endpoint, rdata, "registrar_attempt_without_configured_aors");
		ast_log(LOG_WARNING, "Endpoint '%s' has no configured AORs\n", ast_sorcery_object_get_id(endpoint));
		return PJ_TRUE;
	}

	if (!PJSIP_URI_SCHEME_IS_SIP(rdata->msg_info.to->uri) && !PJSIP_URI_SCHEME_IS_SIPS(rdata->msg_info.to->uri)) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 416, NULL, NULL, NULL);
		ast_sip_report_failed_acl(endpoint, rdata, "registrar_invalid_uri_in_to_received");
		ast_log(LOG_WARNING, "Endpoint '%s' attempted to register to an AOR with a non-SIP URI\n", ast_sorcery_object_get_id(endpoint));
		return PJ_TRUE;
	}

	aor = find_registrar_aor(rdata, endpoint);
	if (!aor) {
		/* We've already responded about not finding an AOR. */
		return PJ_TRUE;
	}

	aor_name = ast_sorcery_object_get_id(aor);

	if (!aor->max_contacts) {
		/* Registration is not permitted for this AOR */
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 403, NULL, NULL, NULL);
		ast_sip_report_req_no_support(endpoint, rdata, "registrar_attempt_without_registration_permitted");
		ast_log(LOG_WARNING, "AOR '%s' has no configured max_contacts. Endpoint '%s' unable to register\n",
			aor_name, ast_sorcery_object_get_id(endpoint));
	} else {
		register_aor(rdata, endpoint, aor, aor_name);
	}
	ao2_ref(aor, -1);
	return PJ_TRUE;
}

/* function pointer to callback needs to be within the module
   in order to avoid problems with an undefined symbol */
static int sip_contact_to_str(void *acp, void *arg, int flags)
{
	return ast_sip_contact_to_str(acp, arg, flags);
}

static int ami_registrations_aor(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;
	struct ast_sip_ami *ami = arg;
	int *count = ami->arg;
	RAII_VAR(struct ast_str *, buf,
		 ast_sip_create_ami_event("InboundRegistrationDetail", ami), ast_free);

	if (!buf) {
		return -1;
	}

	ast_sip_sorcery_object_to_ami(aor, &buf);
	ast_str_append(&buf, 0, "Contacts: ");
	ast_sip_for_each_contact(aor, sip_contact_to_str, &buf);
	ast_str_append(&buf, 0, "\r\n");

	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	(*count)++;
	return 0;
}

static int ami_registrations_endpoint(void *obj, void *arg, int flags)
{
	struct ast_sip_endpoint *endpoint = obj;
	return ast_sip_for_each_aor(
		endpoint->aors, ami_registrations_aor, arg);
}

static int ami_registrations_endpoints(void *arg)
{
	RAII_VAR(struct ao2_container *, endpoints,
		 ast_sip_get_endpoints(), ao2_cleanup);

	if (!endpoints) {
		return 0;
	}

	ao2_callback(endpoints, OBJ_NODATA, ami_registrations_endpoint, arg);
	return 0;
}

static int ami_show_registrations(struct mansession *s, const struct message *m)
{
	int count = 0;
	struct ast_sip_ami ami = { .s = s, .m = m, .arg = &count, .action_id = astman_get_header(m, "ActionID"), };

	astman_send_listack(s, m, "Following are Events for each Inbound "
			    "registration", "start");

	ami_registrations_endpoints(&ami);

	astman_send_list_complete_start(s, m, "InboundRegistrationDetailComplete", count);
	astman_send_list_complete_end(s);
	return 0;
}

#define AMI_SHOW_REGISTRATIONS "PJSIPShowRegistrationsInbound"

static pjsip_module registrar_module = {
	.name = { "Registrar", 9 },
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_APPLICATION,
	.on_rx_request = registrar_on_rx_request,
};

static int load_module(void)
{
	const pj_str_t STR_REGISTER = { "REGISTER", 8 };

	CHECK_PJPROJECT_MODULE_LOADED();

	ast_pjproject_get_buildopt("PJ_MAX_HOSTNAME", "%d", &pj_max_hostname);
	/* As of pjproject 2.4.5, PJSIP_MAX_URL_SIZE isn't exposed yet but we try anyway. */
	ast_pjproject_get_buildopt("PJSIP_MAX_URL_SIZE", "%d", &pjsip_max_url_size);

	CHECK_PJSIP_MODULE_LOADED();

	if (ast_sip_register_service(&registrar_module)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (pjsip_endpt_add_capability(ast_sip_get_pjsip_endpoint(), NULL, PJSIP_H_ALLOW, NULL, 1, &STR_REGISTER) != PJ_SUCCESS) {
		ast_sip_unregister_service(&registrar_module);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_manager_register_xml(AMI_SHOW_REGISTRATIONS, EVENT_FLAG_SYSTEM,
				 ami_show_registrations);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_manager_unregister(AMI_SHOW_REGISTRATIONS);
	ast_sip_unregister_service(&registrar_module);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Registrar Support",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND - 3,
	       );
