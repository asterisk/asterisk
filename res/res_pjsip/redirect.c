/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, Commend International
 *
 * Maximilian Fridrich <m.fridrich@commend.com>
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

#include <pjsip.h>

#include "asterisk/linkedlists.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_redirect.h"

/*!
 * \internal
 * \brief Visited URI tracking for redirect loop detection
 */
struct visited_uri {
	char uri[PJSIP_MAX_URL_SIZE];
	AST_LIST_ENTRY(visited_uri) list;
};

/*!
 * \internal
 * \brief Redirect contact with q-value for prioritization
 */
struct redirect_contact {
	char uri[PJSIP_MAX_URL_SIZE];
	float q_value; /* q-value from Contact header, default 1.0 if not present */
	AST_LIST_ENTRY(redirect_contact) list;
};

/*! \brief List of redirect contacts */
AST_LIST_HEAD_NOLOCK(redirect_contact_list, redirect_contact);

/*!
 * \brief Redirect state structure
 */
struct ast_sip_redirect_state {
	struct ast_sip_endpoint *endpoint;
	int hop_count;
	AST_LIST_HEAD_NOLOCK(, visited_uri) visited_uris;
	struct redirect_contact_list pending_contacts;
};

struct ast_sip_redirect_state *ast_sip_redirect_state_create(
	struct ast_sip_endpoint *endpoint,
	const char *initial_uri)
{
	struct ast_sip_redirect_state *state;
	struct visited_uri *visited;

	state = ast_calloc(1, sizeof(*state));
	if (!state) {
		return NULL;
	}

	state->endpoint = ao2_bump(endpoint);
	state->hop_count = 0;
	AST_LIST_HEAD_INIT_NOLOCK(&state->visited_uris);
	AST_LIST_HEAD_INIT_NOLOCK(&state->pending_contacts);

	/* Add the initial URI to visited list */
	if (initial_uri) {
		visited = ast_calloc(1, sizeof(*visited));
		if (visited) {
			ast_copy_string(visited->uri, initial_uri, sizeof(visited->uri));
			AST_LIST_INSERT_HEAD(&state->visited_uris, visited, list);
		} else {
			ast_log(LOG_WARNING, "Redirect: Memory allocation failed for endpoint '%s'. "
				"Redirect loop detection may be impaired.\n", ast_sorcery_object_get_id(state->endpoint));
		}
	}

	return state;
}

/*!
 * \brief Mapping of SIP method names to their corresponding redirect flags
 */
struct redirect_method_map {
	const char *method_name;
	enum ast_sip_redirect_method flag;
};

static const struct redirect_method_map redirect_methods[] = {
	{ "MESSAGE", AST_SIP_REDIRECT_METHOD_MESSAGE },
};

/*!
 * \internal
 * \brief Check if a SIP method is allowed to follow redirects
 *
 * \param endpoint The SIP endpoint
 * \param method_name The SIP method name from the CSeq header
 *
 * \retval 0 if method is not allowed to follow redirects
 * \retval 1 if method is allowed to follow redirects
 */
static int method_allowed_for_redirect(struct ast_sip_endpoint *endpoint, const pj_str_t *method_name)
{
	int i;

	/* Look up the method in our mapping table */
	for (i = 0; i < ARRAY_LEN(redirect_methods); i++) {
		if (pj_stricmp2(method_name, redirect_methods[i].method_name) == 0) {
			/* Method is recognized, check if it's allowed */
			if (ast_test_flag(&endpoint->follow_redirect_methods, redirect_methods[i].flag)) {
				return 1;
			} else {
				ast_log(LOG_NOTICE, "Received redirect for %s to endpoint '%s', "
					"but %s is not in follow_redirect_methods. Not following redirect.\n",
					redirect_methods[i].method_name,
					ast_sorcery_object_get_id(endpoint), redirect_methods[i].method_name);
				return 0;
			}
		}
	}

	/* Method not recognized/supported for redirects */
	ast_log(LOG_NOTICE, "Received redirect for method %.*s to endpoint '%s', "
		"but this method is not supported in follow_redirect_methods. Not following redirect.\n",
		(int)method_name->slen, method_name->ptr, ast_sorcery_object_get_id(endpoint));
	return 0;
}

int ast_sip_should_redirect(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata)
{
	pjsip_msg *msg;
	pjsip_cseq_hdr *cseq;
	int status_code;

	if (!rdata || !rdata->msg_info.msg || rdata->msg_info.msg->type != PJSIP_RESPONSE_MSG) {
		return 0;
	}

	msg = rdata->msg_info.msg;
	status_code = msg->line.status.code;

	/* Check if it's a 3xx response */
	if (!PJSIP_IS_STATUS_IN_CLASS(status_code, 300)) {
		return 0;
	}

	/* Extract the method from the CSeq header */
	cseq = rdata->msg_info.cseq;
	if (!cseq) {
		ast_log(LOG_WARNING, "Received %d redirect for endpoint '%s', but no CSeq header found\n",
			status_code, ast_sorcery_object_get_id(endpoint));
		return 0;
	}

	/* Check if this method is allowed to follow redirects */
	return method_allowed_for_redirect(endpoint, &cseq->method.name);
}

/*!
 * \internal
 * \brief Check if a URI has already been visited (loop detection)
 */
static int is_uri_visited(const struct ast_sip_redirect_state *state, const char *uri)
{
	struct visited_uri *visited;

	AST_LIST_TRAVERSE(&state->visited_uris, visited, list) {
		if (!strcmp(visited->uri, uri)) {
			return 1;
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief Add a URI to the visited list
 */
static int add_visited_uri(struct ast_sip_redirect_state *state, const char *uri)
{
	struct visited_uri *visited;

	visited = ast_calloc(1, sizeof(*visited));
	if (!visited) {
		return -1;
	}

	ast_copy_string(visited->uri, uri, sizeof(visited->uri));
	AST_LIST_INSERT_TAIL(&state->visited_uris, visited, list);

	return 0;
}

/*!
 * \internal
 * \brief Extract q-value from a Contact header
 *
 * \param contact The Contact header
 * \return The q-value (default 1.0 if not present or invalid)
 */
static float extract_q_value(const pjsip_contact_hdr *contact)
{
	pjsip_param *param;
	static const pj_str_t Q_STR = { "q", 1 };

	/* Search for q parameter in the contact header */
	param = pjsip_param_find(&contact->other_param, &Q_STR);
	if (!param) {
		/* No q parameter, use default */
		return 1.0f;
	}

	/* Parse the q value */
	if (param->value.slen > 0) {
		char q_buf[16];
		float q_val;
		int len = param->value.slen < sizeof(q_buf) - 1 ? param->value.slen : sizeof(q_buf) - 1;
		memcpy(q_buf, param->value.ptr, len);
		q_buf[len] = '\0';

		q_val = ast_sip_parse_qvalue(q_buf);

		return q_val < 0.0f ? 1.0f : q_val;
	}

	/* Invalid q value, use default */
	return 1.0f;
}

/*!
 * \internal
 * \brief Insert a contact into the sorted list by q-value (highest first)
 *
 * \param list The list to insert into
 * \param new_contact The contact to insert
 */
static void insert_contact_sorted(struct redirect_contact_list *list, struct redirect_contact *new_contact)
{
	struct redirect_contact *contact;

	/* Find the insertion point - contacts with higher q values come first */
	AST_LIST_TRAVERSE_SAFE_BEGIN(list, contact, list) {
		if (new_contact->q_value > contact->q_value) {
			/* Insert before this contact */
			AST_LIST_INSERT_BEFORE_CURRENT(new_contact, list);
			return;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* If we get here, insert at the end */
	AST_LIST_INSERT_TAIL(list, new_contact, list);
}

/*!
 * \internal
 * \brief Parse all Contact headers from a 3xx response and create a sorted list
 *
 * \param rdata The redirect response data
 * \param contacts List to populate with parsed contacts
 * \return Number of valid contacts found
 */
static int parse_redirect_contacts(pjsip_rx_data *rdata, struct redirect_contact_list *contacts, const struct ast_sip_redirect_state *state)
{
	pjsip_contact_hdr *contact_hdr;
	pjsip_uri *contact_uri;
	int count = 0;
	void *start = NULL;

	/* Iterate through all Contact headers */
	while ((contact_hdr = (pjsip_contact_hdr *) pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, start))) {
		struct redirect_contact *redirect_contact;
		int len;

		start = contact_hdr->next;

		/* Enforce maximum contact limit to prevent resource exhaustion */
		if (count >= AST_SIP_MAX_REDIRECT_CONTACTS) {
			ast_log(LOG_WARNING, "Redirect: maximum Contact header limit (%d) reached for endpoint '%s'. Ignoring additional contacts\n",
				AST_SIP_MAX_REDIRECT_CONTACTS, ast_sorcery_object_get_id(state->endpoint));
			break;
		}

		if (!contact_hdr->uri) {
			continue;
		}

		contact_uri = (pjsip_uri *)pjsip_uri_get_uri(contact_hdr->uri);

		/* Verify it's a SIP URI */
		if (!PJSIP_URI_SCHEME_IS_SIP(contact_uri) && !PJSIP_URI_SCHEME_IS_SIPS(contact_uri)) {
			ast_debug(1, "Skipping non-SIP/SIPS Contact URI in redirect for endpoint '%s'\n", ast_sorcery_object_get_id(state->endpoint));
			continue;
		}

		/* Allocate a new redirect_contact structure */
		redirect_contact = ast_calloc(1, sizeof(*redirect_contact));
		if (!redirect_contact) {
			ast_log(LOG_ERROR, "Failed to allocate memory for redirect contact for endpoint '%s'.\n", ast_sorcery_object_get_id(state->endpoint));
			continue;
		}

		/* Print the URI */
		len = pjsip_uri_print(PJSIP_URI_IN_REQ_URI, contact_uri,
			redirect_contact->uri, sizeof(redirect_contact->uri) - 1);
		if (len < 1) {
			ast_debug(1, "Contact URI too long for redirect on endpoint '%s'. Skipping.\n", ast_sorcery_object_get_id(state->endpoint));
			ast_free(redirect_contact);
			continue;
		}
		redirect_contact->uri[len] = '\0';

		/* Extract q-value */
		redirect_contact->q_value = extract_q_value(contact_hdr);

		ast_debug(1, "Found redirect Contact: %s (q=%f) for endpoint '%s'.\n", redirect_contact->uri, redirect_contact->q_value,
			ast_sorcery_object_get_id(state->endpoint));

		/* Insert into sorted list */
		insert_contact_sorted(contacts, redirect_contact);
		count++;
	}

	return count;
}

int ast_sip_redirect_parse_3xx(pjsip_rx_data *rdata, struct ast_sip_redirect_state *state)
{
	struct redirect_contact_list redirect_contacts;
	struct redirect_contact *contact;
	int contact_count;
	int status_code = rdata->msg_info.msg->line.status.code;

	ast_debug(1, "Received %d redirect response on endpoint '%s'.\n", status_code, ast_sorcery_object_get_id(state->endpoint));

	/* Check if redirect should be followed based on endpoint configuration */
	if (!ast_sip_should_redirect(state->endpoint, rdata)) {
		return -1;
	}

	/* Check hop limit */
	if (state->hop_count >= AST_SIP_MAX_REDIRECT_HOPS) {
		ast_log(LOG_WARNING, "Redirect hop limit (%d) reached for endpoint '%s'. Not following redirect.\n",
		  AST_SIP_MAX_REDIRECT_HOPS, ast_sorcery_object_get_id(state->endpoint));
		return -1;
	}

	/* Parse all Contact headers and sort by q-value */
	AST_LIST_HEAD_INIT_NOLOCK(&redirect_contacts);
	contact_count = parse_redirect_contacts(rdata, &redirect_contacts, state);

	if (contact_count == 0) {
		ast_log(LOG_WARNING, "Received %d redirect without valid Contact headers for endpoint '%s'. Cannot follow redirect.\n",
			status_code, ast_sorcery_object_get_id(state->endpoint));
		return -1;
	}

	/* Filter out contacts that would create loops */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&redirect_contacts, contact, list) {
		if (is_uri_visited(state, contact->uri)) {
			ast_log(LOG_WARNING, "Redirect: skipping Contact '%s' for endpoint '%s' (would create loop)\n", contact->uri,
				ast_sorcery_object_get_id(state->endpoint));
			AST_LIST_REMOVE_CURRENT(list);
			ast_free(contact);
			contact_count--;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (contact_count == 0) {
		ast_log(LOG_WARNING, "Redirect: all Contact URIs would create loops for endpoint '%s'. Not following redirect.\n",
			ast_sorcery_object_get_id(state->endpoint));
		return -1;
	}

	/* Move all contacts to pending_contacts list */
	while ((contact = AST_LIST_REMOVE_HEAD(&redirect_contacts, list))) {
		AST_LIST_INSERT_TAIL(&state->pending_contacts, contact, list);
	}

	/* Increment hop count */
	state->hop_count++;

	return 0;
}

int ast_sip_redirect_get_next_uri(struct ast_sip_redirect_state *state, char **uri_out)
{
	struct redirect_contact *contact;

	if (!uri_out) {
		return -1;
	}

	/* Get the first contact from the pending list */
	contact = AST_LIST_REMOVE_HEAD(&state->pending_contacts, list);
	if (!contact) {
		return -1;
	}

	/* Allocate and return the URI string */
	*uri_out = ast_strdup(contact->uri);
	if (!*uri_out) {
		ast_free(contact);
		return -1;
	}

	/* Add to visited list to prevent loops */
	if (add_visited_uri(state, contact->uri)) {
		ast_log(LOG_WARNING, "Failed to add URI to visited list for endpoint '%s'. Loop detection may be impaired.\n",
		  ast_sorcery_object_get_id(state->endpoint));
	}

	ast_free(contact);
	return 0;
}

int ast_sip_redirect_check_loop(const struct ast_sip_redirect_state *state, const char *uri)
{
	return is_uri_visited(state, uri);
}

int ast_sip_redirect_get_hop_count(const struct ast_sip_redirect_state *state)
{
	return state->hop_count;
}

struct ast_sip_endpoint *ast_sip_redirect_get_endpoint(const struct ast_sip_redirect_state *state)
{
	return state->endpoint;
}

void ast_sip_redirect_state_destroy(struct ast_sip_redirect_state *state)
{
	struct visited_uri *visited;
	struct redirect_contact *contact;

	if (!state) {
		return;
	}

	ao2_cleanup(state->endpoint);

	while ((visited = AST_LIST_REMOVE_HEAD(&state->visited_uris, list))) {
		ast_free(visited);
	}

	while ((contact = AST_LIST_REMOVE_HEAD(&state->pending_contacts, list))) {
		ast_free(contact);
	}

	ast_free(state);
}
