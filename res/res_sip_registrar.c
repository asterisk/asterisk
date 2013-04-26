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
	<depend>res_sip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_sip.h"
#include "asterisk/module.h"

/*! \brief Internal function which returns the expiration time for a contact */
static int registrar_get_expiration(const struct ast_sip_aor *aor, const pjsip_contact_hdr *contact, const pjsip_rx_data *rdata)
{
	pjsip_expires_hdr *expires;
	int expiration = aor->default_expiration;

	if (contact->expires != -1) {
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
	pjsip_uri *uri;
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
		int expiration;
		RAII_VAR(struct ast_sip_contact *, existing, NULL, ao2_cleanup);

		if (contact->star) {
			/* The expiration MUST be 0 when a '*' contact is used and there must be no other contact */
			if ((contact->expires != 0) || previous) {
				pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), details.pool);
				return -1;
			}
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
		expiration = registrar_get_expiration(aor, contact, rdata);

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

/*! \brief Internal function used to delete all contacts from an AOR */
static int registrar_delete_contact(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;

	ast_sip_location_delete_contact(contact);

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

static pj_bool_t registrar_on_rx_request(struct pjsip_rx_data *rdata)
{
	struct ast_sip_endpoint *endpoint = ast_pjsip_rdata_get_endpoint(rdata);
	pjsip_sip_uri *uri;
	char user_name[64], domain_name[64];
	char *configured_aors, *aor_name;
	RAII_VAR(struct ast_sip_aor *, aor, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);
	int added = 0, updated = 0, deleted = 0;
	pjsip_contact_hdr *contact_hdr = NULL;
	struct registrar_contact_details details = { 0, };
	pjsip_tx_data *tdata;
	pjsip_response_addr addr;

	if (pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_register_method) || !endpoint) {
		return PJ_FALSE;
	}

	if (ast_strlen_zero(endpoint->aors)) {
		/* Short circuit early if the endpoint has no AORs configured on it, which means no registration possible */
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 403, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	if (!PJSIP_URI_SCHEME_IS_SIP(rdata->msg_info.to->uri) && !PJSIP_URI_SCHEME_IS_SIPS(rdata->msg_info.to->uri)) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 416, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	uri = pjsip_uri_get_uri(rdata->msg_info.to->uri);
	ast_copy_pj_str(user_name, &uri->user, sizeof(user_name));
	ast_copy_pj_str(domain_name, &uri->host, sizeof(domain_name));

	configured_aors = ast_strdupa(endpoint->aors);

	/* Iterate the configured AORs to see if the user or the user+domain match */
	while ((aor_name = strsep(&configured_aors, ","))) {
		char id[AST_UUID_STR_LEN];
		RAII_VAR(struct ast_sip_domain_alias *, alias, NULL, ao2_cleanup);

		snprintf(id, sizeof(id), "%s@%s", user_name, domain_name);
		if (!strcmp(aor_name, id)) {
			break;
		}

		if ((alias = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "domain_alias", domain_name))) {
			snprintf(id, sizeof(id), "%s@%s", user_name, alias->domain);
			if (!strcmp(aor_name, id)) {
				break;
			}
		}

		if (!strcmp(aor_name, user_name)) {
			break;
		}
	}

	if (ast_strlen_zero(aor_name) || !(aor = ast_sip_location_retrieve_aor(aor_name))) {
		/* The provided AOR name was not found (be it within the configuration or sorcery itself) */
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 404, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	if (!aor->max_contacts) {
		/* Registration is not permitted for this AOR */
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 403, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	/* Retrieve the current contacts, we'll need to know whether to update or not */
	contacts = ast_sip_location_retrieve_aor_contacts(aor);

	/* So we don't count static contacts against max_contacts we prune them out from the container */
	ao2_callback(contacts, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE, registrar_prune_static, NULL);

	if (registrar_validate_contacts(rdata, contacts, aor, &added, &updated, &deleted)) {
		/* The provided Contact headers do not conform to the specification */
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 400, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	if (((added - deleted) + (!aor->remove_existing ? ao2_container_count(contacts) : 0)) > aor->max_contacts) {
		/* Enforce the maximum number of contacts */
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 403, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	if (!(details.pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "Contact Comparison", 256, 256))) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
		return PJ_TRUE;
	}

	/* Iterate each provided Contact header and add, update, or delete */
	while ((contact_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, contact_hdr ? contact_hdr->next : NULL))) {
		int expiration;
		char contact_uri[PJSIP_MAX_URL_SIZE];
		RAII_VAR(struct ast_sip_contact *, contact, NULL, ao2_cleanup);

		if (contact_hdr->star) {
			/* A star means to unregister everything, so do so for the possible contacts */
			ao2_callback(contacts, OBJ_NODATA | OBJ_MULTIPLE, registrar_delete_contact, NULL);
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

			ast_sip_location_add_contact(aor, contact_uri, ast_tvadd(ast_tvnow(), ast_samp2tv(expiration, 1)));
			ast_verb(3, "Added contact '%s' to AOR '%s' with expiration of %d seconds\n",
				contact_uri, aor_name, expiration);
		} else if (expiration) {
			RAII_VAR(struct ast_sip_contact *, updated, ast_sorcery_copy(ast_sip_get_sorcery(), contact), ao2_cleanup);

			updated->expiration_time = ast_tvadd(ast_tvnow(), ast_samp2tv(expiration, 1));

			ast_sip_location_update_contact(updated);
			ast_debug(3, "Refreshed contact '%s' on AOR '%s' with new expiration of %d seconds\n",
				contact_uri, aor_name, expiration);
		} else {
			ast_sip_location_delete_contact(contact);
			ast_verb(3, "Removed contact '%s' from AOR '%s' due to request\n", contact_uri, aor_name);
		}
	}

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), details.pool);

	/* If the AOR is configured to remove any existing contacts that have not been updated/added as a result of this REGISTER
	 * do so
	 */
	if (aor->remove_existing) {
		ao2_callback(contacts, OBJ_NODATA | OBJ_MULTIPLE, registrar_delete_contact, NULL);
	}

	/* Update the contacts as things will probably have changed */
	ao2_cleanup(contacts);
	contacts = ast_sip_location_retrieve_aor_contacts(aor);

	/* Send a response containing all of the contacts (including static) that are present on this AOR */
	if (pjsip_endpt_create_response(ast_sip_get_pjsip_endpoint(), rdata, 200, NULL, &tdata) != PJ_SUCCESS) {
		return PJ_TRUE;
	}

	/* Add the date header to the response, some UAs use this to set their date and time */
	registrar_add_date_header(tdata);

	ao2_callback(contacts, 0, registrar_add_contact, tdata);

	if (pjsip_get_response_addr(tdata->pool, rdata, &addr) == PJ_SUCCESS) {
		pjsip_endpt_send_response(ast_sip_get_pjsip_endpoint(), &addr, tdata, NULL, NULL);
	} else {
		pjsip_tx_data_dec_ref(tdata);
	}

	return PJ_TRUE;
}

static pjsip_module registrar_module = {
	.name = { "Registrar", 9 },
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_APPLICATION,
	.on_rx_request = registrar_on_rx_request,
};

static int load_module(void)
{
	const pj_str_t STR_REGISTER = { "REGISTER", 8 };

	if (ast_sip_register_service(&registrar_module)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (pjsip_endpt_add_capability(ast_sip_get_pjsip_endpoint(), NULL, PJSIP_H_ALLOW, NULL, 1, &STR_REGISTER) != PJ_SUCCESS) {
		ast_sip_unregister_service(&registrar_module);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_service(&registrar_module);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "SIP Registrar Support",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
