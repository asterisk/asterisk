/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Sangoma Technologies Corporation
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

/*** MODULEINFO
	<depend>res_geolocation</depend>
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_session</depend>
	<depend>chan_pjsip</depend>
	<depend>libxml2</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/module.h"
#include "asterisk/xml.h"
#include "asterisk/res_geolocation.h"

#include <pjsip_ua.h>
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"

static pj_str_t GEOLOCATION_HDR;

static int find_pidf(const char *session_name, struct pjsip_rx_data *rdata, char *geoloc_uri,
	char **pidf_body, unsigned int *pidf_len)
{
	char *local_uri = ast_strdupa(geoloc_uri);
	char *ra = NULL;
	/*
	 * If the URI is "cid" then we're going to search for a pidf document
	 * in the body of the message.  If there's no body, there's no point.
	 */
	if (!rdata->msg_info.msg->body) {
		ast_log(LOG_WARNING, "%s: There's no message body in which to search for '%s'.  Skipping\n",
			session_name, geoloc_uri);
		return -1;
	}

	if (local_uri[0] == '<') {
		local_uri++;
	}
	ra = strchr(local_uri, '>');
	if (ra) {
		*ra = '\0';
	}

	/*
	 * If the message content type is 'application/pidf+xml', then the pidf is
	 * the only document in the message and we'll just parse the entire body
	 * as xml.  If it's 'multipart/mixed' then we have to find the part that
	 * has a Content-ID header value matching the URI.
	 */
	if (ast_sip_are_media_types_equal(&rdata->msg_info.ctype->media,
		&pjsip_media_type_application_pidf_xml)) {
		*pidf_body = rdata->msg_info.msg->body->data;
		*pidf_len = rdata->msg_info.msg->body->len;
	} else if (ast_sip_are_media_types_equal(&rdata->msg_info.ctype->media,
		&pjsip_media_type_multipart_mixed)) {
		pj_str_t cid = pj_str(local_uri);
		pjsip_multipart_part *mp = pjsip_multipart_find_part_by_cid_str(
			rdata->tp_info.pool, rdata->msg_info.msg->body, &cid);

		if (!mp) {
			ast_log(LOG_WARNING, "%s: A Geolocation header was found with URI '%s'"
				" but the associated multipart part was not found in the message body.  Skipping URI",
				session_name, geoloc_uri);
			return -1;
		}
		*pidf_body = mp->body->data;
		*pidf_len = mp->body->len;
	} else {
		ast_log(LOG_WARNING, "%s: A Geolocation header was found with URI '%s'"
			" but no pidf document with that content id was found.  Skipping URI",
			session_name, geoloc_uri);
		return -1;
	}

	return 0;
}


static int handle_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	const char *session_name = (session ? ast_sip_session_get_name(session) : "NULL_SESSION");
	struct ast_sip_endpoint *endpoint = (session ? session->endpoint : NULL);
	struct ast_channel *channel = (session ? session->channel : NULL);
	RAII_VAR(struct ast_geoloc_profile *, config_profile, NULL, ao2_cleanup);
	RAII_VAR(struct ast_geoloc_eprofile *, eprofile, NULL, ao2_cleanup);
	RAII_VAR(struct ast_datastore *, ds, NULL, ast_datastore_free);
	size_t eprofile_count = 0;
	char *geoloc_hdr_value = NULL;
	char *geoloc_uri = NULL;
	int rc = 0;
	RAII_VAR(struct ast_str *, buf, ast_str_create(1024), ast_free);
	pjsip_generic_string_hdr *geoloc_hdr = NULL;
	SCOPE_ENTER(3, "%s\n", session_name);

	if (!session) {
		SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING, "%s: session is NULL!!!.  Skipping.\n",
			session_name);
	}
	if (!endpoint) {
		SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING, "%s: Session has no endpoint.  Skipping.\n",
			session_name);
	}

	if (!channel) {
		SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING, "%s: Session has no channel.  Skipping.\n",
			session_name);
	}

	if (!rdata) {
		SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING, "%s: Session has no rdata.  Skipping.\n",
			session_name);
	}

	geoloc_hdr = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &GEOLOCATION_HDR, NULL);

	if (!geoloc_hdr) {
		ast_trace(4, "%s: Message has no Geolocation header\n", session_name);
	} else {
		ast_trace(4, "%s: Geolocation: " PJSTR_PRINTF_SPEC "\n", session_name,
			PJSTR_PRINTF_VAR(geoloc_hdr->hvalue));
	}

	if (ast_strlen_zero(endpoint->geoloc_incoming_call_profile)) {
		if (geoloc_hdr) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_NOTICE, "%s: Message has Geolocation header '"
				PJSTR_PRINTF_SPEC "' but endpoint has no geoloc_incoming_call_profile. "
				"Geolocation info discarded.\n", session_name,
				PJSTR_PRINTF_VAR(geoloc_hdr->hvalue));
		} else {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_NOTICE, "%s: Endpoint has no geoloc_incoming_call_profile. "
				"Skipping.\n", session_name);
		}
	}

	config_profile = ast_geoloc_get_profile(endpoint->geoloc_incoming_call_profile);
	if (!config_profile) {
		if (geoloc_hdr) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_NOTICE, "%s: Message has Geolocation header '"
				PJSTR_PRINTF_SPEC "' but endpoint's geoloc_incoming_call_profile doesn't exist. "
				"Geolocation info discarded.\n", session_name,
				PJSTR_PRINTF_VAR(geoloc_hdr->hvalue));
		} else {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_NOTICE, "%s: Message has no Geolocation header and endpoint has "
				" an invalid geoloc_incoming_call_profile.  Nothing to do..\n", session_name);
		}
	}

	ds = ast_geoloc_datastore_create(session_name);
	if (!ds) {
		SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING,
			"%s: Couldn't allocate a geoloc datastore\n", session_name);
	}

	/*
	 * We want the datastore to pass through the dialplan and the core
	 * so we need to turn inheritance on.
	 */
	ast_geoloc_datastore_set_inheritance(ds, 1);

	switch (config_profile->action) {
	case AST_GEOLOC_ACT_DISCARD_INCOMING:
		if (geoloc_hdr) {
			ast_trace(4, "%s: Profile '%s' location_disposition is 'discard_incoming' so "
				"discarding Geolocation: " PJSTR_PRINTF_SPEC "\n", session_name,
				ast_sorcery_object_get_id(config_profile),
				PJSTR_PRINTF_VAR(geoloc_hdr->hvalue));
		} else {
			ast_trace(4, "%s: Profile '%s' location_disposition is 'discard_incoming' but there was no Geolocation header"
				"so there's nothing to discard\n",
				session_name, ast_sorcery_object_get_id(config_profile));
		}

		eprofile = ast_geoloc_eprofile_create_from_profile(config_profile);
		if (!eprofile) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING, "%s: Unable to create eprofile from "
				"profile '%s'\n", session_name, ast_sorcery_object_get_id(config_profile));
		}

		if (!eprofile->effective_location) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_NOTICE, "%s: Configured profile has no effective location.  Skipping."
				"profile '%s'\n", session_name, ast_sorcery_object_get_id(eprofile));
		}

		rc = ast_geoloc_datastore_add_eprofile(ds, eprofile);
		if (rc <= 0) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING,
				"%s: Couldn't add eprofile '%s' to datastore\n", session_name,
				eprofile->id);
		}

		ast_channel_lock(channel);
		ast_channel_datastore_add(channel, ds);
		ast_channel_unlock(channel);
		/* We gave the datastore to the channel so don't let RAII_VAR clean it up. */
		ds = NULL;

		ast_trace(4, "ep: '%s' EffectiveLoc: %s\n", eprofile->id, ast_str_buffer(
			ast_variable_list_join(eprofile->effective_location, ",", "=", NULL, &buf)));
		ast_str_reset(buf);

		/* We discarded the Geolocation header so there's no need to go on. */
		SCOPE_EXIT_RTN_VALUE(0, "%s: Added geoloc datastore with 1 eprofile from config\n",
			session_name);

	case AST_GEOLOC_ACT_DISCARD_CONFIG:
		if (geoloc_hdr) {
			ast_trace(4, "%s: Profile '%s' location_disposition is 'discard_config' so "
				"discarding config profile\n", session_name, ast_sorcery_object_get_id(config_profile));
			/* We process the Geolocation header down below. */
		} else {
			/* Discarded the config and there's no Geolocation header so we're done. */
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_NOTICE, "%s: Profile '%s' location_disposition is 'discard_config' but "
				"there was no Geolocation header so there's nothing left to process\n",
				session_name, ast_sorcery_object_get_id(config_profile));
		}
		break;

	case AST_GEOLOC_ACT_PREFER_CONFIG:
		eprofile = ast_geoloc_eprofile_create_from_profile(config_profile);
		if (!eprofile) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING, "%s: Unable to create eprofile from "
				"profile '%s'\n", session_name, ast_sorcery_object_get_id(config_profile));
		}

		if (!eprofile->effective_location) {
			if (geoloc_hdr) {
				ast_trace(4, "%s: Profile '%s' location_disposition is 'prefer_config' but the configured"
					"eprofile has no location information.  Falling back to Geolocation: "
					PJSTR_PRINTF_SPEC "\n", session_name, ast_sorcery_object_get_id(config_profile),
				PJSTR_PRINTF_VAR(geoloc_hdr->hvalue));
				/* We process the Geolocation header down below. */
			} else {
				SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_NOTICE, "%s: Configured profile '%s' has no effective location"
					" and there was no Geolocation header.  Skipping.\n",
					session_name, ast_sorcery_object_get_id(eprofile));
			}
			break;
		}

		rc = ast_geoloc_datastore_add_eprofile(ds, eprofile);
		if (rc <= 0) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING,
				"%s: Couldn't add eprofile '%s' to datastore\n", session_name,
				eprofile->id);
		}

		ast_channel_lock(channel);
		ast_channel_datastore_add(channel, ds);
		ast_channel_unlock(channel);
		/* We gave the datastore to the channel so don't let RAII_VAR clean it up. */
		ds = NULL;

		if (geoloc_hdr) {
			ast_trace(4, "%s: Profile '%s' location_disposition is 'prefer_config' so "
				"discarding Geolocation: " PJSTR_PRINTF_SPEC "\n",
				session_name, ast_sorcery_object_get_id(config_profile), PJSTR_PRINTF_VAR(geoloc_hdr->hvalue));
		}

		ast_trace(4, "ep: '%s' EffectiveLoc: %s\n", eprofile->id, ast_str_buffer(
			ast_variable_list_join(eprofile->effective_location, ",", "=", NULL, &buf)));
		ast_str_reset(buf);

		/* We discarded the Geolocation header so there's no need to go on. */
		SCOPE_EXIT_RTN_VALUE(0, "%s: Added geoloc datastore with 1 eprofile from config\n",
			session_name);

	case AST_GEOLOC_ACT_PREFER_INCOMING:
		if (geoloc_hdr) {
			ast_trace(4, "%s: Profile '%s' location_disposition is 'replace' so "
				"we don't need to do anything with the configured profile", session_name,
				ast_sorcery_object_get_id(config_profile));
		} else {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_NOTICE,
				"%s: Profile '%s' location_disposition is 'replace' but there's "
				"no Geolocation header and therefore no location info to replace"
				"it with\n", session_name, ast_sorcery_object_get_id(config_profile));
		}
	}

	geoloc_hdr_value = ast_alloca(geoloc_hdr->hvalue.slen + 1);
	ast_copy_pj_str(geoloc_hdr_value, &geoloc_hdr->hvalue, geoloc_hdr->hvalue.slen + 1);

	/*
	 * From RFC-6442:
	 * Geolocation-header = "Geolocation" HCOLON locationValue
	 *                      *( COMMA locationValue )
	 * locationValue      = LAQUOT locationURI RAQUOT
	 *                      *(SEMI geoloc-param)
	 * locationURI        = sip-URI / sips-URI / pres-URI
	 *                        / http-URI / https-URI
	 *	                      / cid-url ; (from RFC 2392)
	 *                        / absoluteURI ; (from RFC 3261)
	 */
	while((geoloc_uri = ast_strsep(&geoloc_hdr_value, ',', AST_STRSEP_TRIM))) {
		/* geoloc_uri should now be <scheme:location>[;loc-src=fqdn] */
		char *pidf_body = NULL;
		unsigned int pidf_len = 0;
		struct ast_xml_doc *incoming_doc = NULL;
		struct ast_geoloc_eprofile *eprofile = NULL;
		int rc = 0;

		ast_trace(4, "Processing URI '%s'\n", geoloc_uri);

		if (geoloc_uri[0] != '<' || strchr(geoloc_uri, '>') == NULL) {
			ast_log(LOG_WARNING, "%s: Geolocation header has bad URI '%s'.  Skipping\n", session_name,
				geoloc_uri);
			continue;
		}
		/*
		 * If the URI isn't "cid" then we're just going to pass it through.
		 */
		if (!ast_begins_with(geoloc_uri, "<cid:")) {
			ast_trace(4, "Processing URI '%s'\n", geoloc_uri);

			eprofile = ast_geoloc_eprofile_create_from_uri(geoloc_uri, session_name);
			if (!eprofile) {
				ast_log(LOG_WARNING, "%s: Unable to create effective profile for URI '%s'.  Skipping\n",
					session_name, geoloc_uri);
				continue;
			}
		} else {
			ast_trace(4, "Processing PIDF-LO '%s'\n", geoloc_uri);

			rc = find_pidf(session_name, rdata, geoloc_uri, &pidf_body, &pidf_len);
			if (rc != 0 || !pidf_body || pidf_len == 0) {
				continue;
			}
			ast_trace(5, "Processing PIDF-LO "PJSTR_PRINTF_SPEC "\n", (int)pidf_len, pidf_body);

			incoming_doc = ast_xml_read_memory(pidf_body, pidf_len);
			if (!incoming_doc) {
				ast_log(LOG_WARNING, "%s: Unable to parse pidf document for URI '%s'\n",
					session_name, geoloc_uri);
				continue;
			}

			eprofile = ast_geoloc_eprofile_create_from_pidf(incoming_doc, geoloc_uri, session_name);
		}
		eprofile->action = config_profile->action;

		ast_trace(4, "Processing URI '%s'.  Adding to datastore\n", geoloc_uri);
		rc = ast_geoloc_datastore_add_eprofile(ds, eprofile);
		ao2_ref(eprofile, -1);
		if (rc <= 0) {
			ast_log(LOG_WARNING, "%s: Unable to add effective profile for URI '%s' to datastore.  Skipping\n",
				session_name, geoloc_uri);
		}
	}

	if (config_profile->action == AST_GEOLOC_ACT_PREFER_CONFIG) {
		ast_trace(4, "%s: Profile '%s' location_disposition is 'prepend' so "
			"adding to datastore first", session_name, ast_sorcery_object_get_id(config_profile));

		eprofile = ast_geoloc_eprofile_create_from_profile(config_profile);
		if (!eprofile) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING, "%s: Unable to create eprofile from"
				" profile '%s'\n", session_name, ast_sorcery_object_get_id(config_profile));
		}

		rc = ast_geoloc_datastore_add_eprofile(ds, eprofile);
		if (rc <= 0) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING,
				"%s: Couldn't add eprofile '%s' to datastore\n", session_name,
				eprofile->id);
		}
	}

	eprofile_count = ast_geoloc_datastore_size(ds);
	if (eprofile_count == 0) {
		SCOPE_EXIT_RTN_VALUE(0,
			"%s: Unable to add any effective profiles.  Not adding datastore to channel.\n",
			session_name);
	}

	ast_channel_lock(channel);
	ast_channel_datastore_add(channel, ds);
	ast_channel_unlock(channel);
	ds = NULL;

	SCOPE_EXIT_RTN_VALUE(0, "%s: Added geoloc datastore with %" PRIu64 " eprofiles\n",
		session_name, eprofile_count);
}

static int add_pidf_to_tdata(struct ast_datastore *tempds, struct ast_channel *channel,
	struct ast_vector_string *uris, int pidf_index, struct pjsip_tx_data *tdata, const char *session_name)
{
	static const pj_str_t from_name = { "From", 4};
	static const pj_str_t cid_name = { "Content-ID", 10 };

	pjsip_sip_uri *sip_uri;
	pjsip_generic_string_hdr *cid;
	pj_str_t cid_value;
	pjsip_from_hdr *from = pjsip_msg_find_hdr_by_name(tdata->msg, &from_name, NULL);
	pjsip_sdp_info *tdata_sdp_info;
	pjsip_msg_body *multipart_body = NULL;
	pjsip_multipart_part *pidf_part;
	pj_str_t pidf_body_text;
	char id[6];
	size_t alloc_size;
	RAII_VAR(char *, base_cid, NULL, ast_free);
	const char *final;
	int rc = 0;
	RAII_VAR(struct ast_str *, buf, ast_str_create(1024), ast_free);
	SCOPE_ENTER(3, "%s\n", session_name);

	/*
	 * ast_geoloc_eprofiles_to_pidf() takes the datastore with all of the eprofiles
	 * in it, skips over the ones not needing PIDF processing and combines the
	 * rest into one document.
	 */
	final = ast_geoloc_eprofiles_to_pidf(tempds, channel, &buf, session_name);
	ast_trace(5, "Final pidf: \n%s\n", final);

	/*
	 * There _should_ be an SDP already attached to the tdata at this point
	 * but maybe not.  If we can find an existing one, we'll convert the tdata
	 * body into a multipart body and add the SDP as the first part.  Then we'll
	 * create another part to hold the PIDF.
	 *
	 * If we don't find one, we're going to create an empty multipart body
	 * and add the PIDF part to it.
	 *
	 * Technically, if we only have the PIDF, we don't need a multipart
	 * body to hold it but that means we'd have to add the Content-ID header
	 * to the main SIP message.  Since it's unlikely, it's just better to
	 * add the multipart body and leave the rest of the processing unchanged.
	 */
	tdata_sdp_info = pjsip_tdata_get_sdp_info(tdata);
	if (tdata_sdp_info->sdp) {
		ast_trace(4, "body: %p %u\n", tdata_sdp_info->sdp, (unsigned)tdata_sdp_info->sdp_err);

		rc = pjsip_create_multipart_sdp_body(tdata->pool, tdata_sdp_info->sdp, &multipart_body);
		if (rc != PJ_SUCCESS) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_ERROR, "%s: Unable to create sdp multipart body\n",
				session_name);
		}
	} else {
	    multipart_body = pjsip_multipart_create(tdata->pool, &pjsip_media_type_multipart_mixed, NULL);
	}

	pidf_part = pjsip_multipart_create_part(tdata->pool);
	pj_cstr(&pidf_body_text, final);
	pidf_part->body = pjsip_msg_body_create(tdata->pool, &pjsip_media_type_application_pidf_xml.type,
		&pjsip_media_type_application_pidf_xml.subtype, &pidf_body_text);

    pjsip_multipart_add_part(tdata->pool, multipart_body, pidf_part);

	sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(from->uri);
	alloc_size = sizeof(id) + pj_strlen(&sip_uri->host) + 2;
	base_cid = ast_malloc(alloc_size);
	sprintf(base_cid, "%s@%.*s",
			ast_generate_random_string(id, sizeof(id)),
			(int) pj_strlen(&sip_uri->host), pj_strbuf(&sip_uri->host));

	ast_str_set(&buf, 0, "cid:%s", base_cid);
	ast_trace(4, "cid: '%s' uri: '%s' pidf_index: %d\n", base_cid, ast_str_buffer(buf), pidf_index);

	AST_VECTOR_INSERT_AT(uris, pidf_index, ast_strdup(ast_str_buffer(buf)));

	cid_value.ptr = pj_pool_alloc(tdata->pool, alloc_size);
	cid_value.slen = sprintf(cid_value.ptr, "<%s>", base_cid);

	cid = pjsip_generic_string_hdr_create(tdata->pool, &cid_name, &cid_value);

	pj_list_insert_after(&pidf_part->hdr, cid);

    tdata->msg->body = multipart_body;

	SCOPE_EXIT_RTN_VALUE(0, "%s: PIDF-LO added with cid '%s'\n", session_name, base_cid);
}

static void handle_outgoing_request(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	const char *session_name = ast_sip_session_get_name(session);
	struct ast_sip_endpoint *endpoint = session->endpoint;
	struct ast_channel *channel = session->channel;
	RAII_VAR(struct ast_geoloc_profile *, config_profile, NULL, ao2_cleanup);
	RAII_VAR(struct ast_geoloc_eprofile *, config_eprofile, NULL, ao2_cleanup);
	RAII_VAR(struct ast_str *, buf, ast_str_create(1024), ast_free);
	RAII_VAR(struct ast_datastore *, tempds, NULL, ast_datastore_free);
	struct ast_datastore *ds = NULL;  /* The channel cleans up ds */
	struct ast_vector_string uris;
	pjsip_msg_body *orig_body;
	pjsip_generic_string_hdr *geoloc_hdr;
	int i;
	int eprofile_count = 0;
	int pidf_index = -1;
	int geoloc_routing = 0;
	int rc = 0;
	const char *final;
	SCOPE_ENTER(3, "%s\n", session_name);

	if (!buf) {
		SCOPE_EXIT_LOG_RTN(LOG_WARNING, "%s: Unable to allocate buf\n",
			session_name);
	}

	if (!endpoint) {
		SCOPE_EXIT_LOG_RTN(LOG_WARNING, "%s: Session has no endpoint.  Skipping.\n",
			session_name);
	}

	if (!channel) {
		SCOPE_EXIT_LOG_RTN(LOG_WARNING, "%s: Session has no channel.  Skipping.\n",
			session_name);
	}

	if (ast_strlen_zero(endpoint->geoloc_outgoing_call_profile)) {
			SCOPE_EXIT_LOG_RTN(LOG_NOTICE, "%s: Endpoint has no geoloc_outgoing_call_profile. "
				"Skipping.\n", session_name);
	}

	config_profile = ast_geoloc_get_profile(endpoint->geoloc_outgoing_call_profile);
	if (!config_profile) {
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Endpoint's geoloc_outgoing_call_profile doesn't exist. "
			"Geolocation info discarded.\n", session_name);
	}

	config_eprofile = ast_geoloc_eprofile_create_from_profile(config_profile);
	if (!config_eprofile) {
		SCOPE_EXIT_LOG_RTN(LOG_WARNING, "%s: Unable to create eprofile from "
			"profile '%s'\n", session_name, ast_sorcery_object_get_id(config_profile));
	}

	if (config_profile->action != AST_GEOLOC_ACT_PREFER_INCOMING) {
		ds = ast_geoloc_datastore_find(channel);
		if (!ds) {
			ast_trace(4, "%s: There was no geoloc datastore\n", session_name);
		} else {
			eprofile_count = ast_geoloc_datastore_size(ds);
			ast_trace(4, "%s: There are %d geoloc profiles on this channel\n", session_name,
				eprofile_count);
		}
	}

	/*
	 * We don't want to alter the datastore that may (or may not) be on
	 * the channel so we're going to create a temporary one to hold the
	 * config eprofile plus any in the channel datastore.  Technically
	 * we could just use a vector but the datastore already has the logic
	 * to release all the eprofile references and the datastore itself.
	 */
	tempds = ast_geoloc_datastore_create("temp");
	if (!ds) {
		ast_trace(4, "%s: There are no geoloc profiles on this channel\n", session_name);
		ast_geoloc_datastore_add_eprofile(tempds, config_eprofile);
	} else {
		if (config_profile->action == AST_GEOLOC_ACT_PREFER_CONFIG) {
			ast_trace(4, "%s: prepending config_eprofile\n", session_name);
			ast_geoloc_datastore_add_eprofile(tempds, config_eprofile);
		}
		for (i = 0; i < eprofile_count; i++) {
			struct ast_geoloc_eprofile *ep = ast_geoloc_datastore_get_eprofile(ds, i);
			ast_trace(4, "%s: adding eprofile '%s' from channel\n", session_name, ep->id);
			ast_geoloc_datastore_add_eprofile(tempds, ep);
		}
		if (config_profile->action == AST_GEOLOC_ACT_PREFER_INCOMING) {
			ast_trace(4, "%s: appending config_eprofile\n", session_name);
			ast_geoloc_datastore_add_eprofile(tempds, config_eprofile);
		}
	}

	eprofile_count = ast_geoloc_datastore_size(tempds);
	if (eprofile_count == 0) {
		SCOPE_EXIT_RTN("%s: There are no profiles left to send\n", session_name);
	}
	ast_trace(4, "%s: There are now %d geoloc profiles to be sent\n", session_name,
		eprofile_count);

	/*
	 * This vector is going to accumulate all of the URIs that
	 * will need to go on the Geolocation header.
	 */
	rc = AST_VECTOR_INIT(&uris, 2);
	if (rc != 0) {
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Unable to allocate memory for vector\n", session_name);
	}

	/*
	 * It's possible that we have a list of eprofiles that have both "pass-by-reference (external URI)"
	 * and "pass by value (to go in PIDF)" eprofiles.  The ones that just need a URI added to the
	 * Geolocation header get added to the "uris" vector in this loop. The ones that result in a
	 * PIDF though, need to be combined into a single PIDF-LO document so we're just going to
	 * save the first one's index so we can insert the "cid" header in the right place, then
	 * we'll send the whole list off to add_pidf_to_tdata() so they can be combined into a
	 * single document.
	 */

	for (i = 0; i < eprofile_count; i++) {
		struct ast_geoloc_eprofile *ep = ast_geoloc_datastore_get_eprofile(tempds, i);
		ast_geoloc_eprofile_refresh_location(ep);

		ast_trace(4, "ep: '%s' EffectiveLoc: %s\n", ep->id, ast_str_buffer(
			ast_variable_list_join(ep->effective_location, ",", "=", NULL, &buf)));
		ast_str_reset(buf);

		if (ep->format == AST_GEOLOC_FORMAT_URI) {
			final = ast_geoloc_eprofile_to_uri(ep, channel, &buf, session_name);
			ast_trace(4, "URI: %s\n", final);
			AST_VECTOR_APPEND(&uris, ast_strdup(final));
			ast_str_reset(buf);
		} else {
			/*
			 * If there are GML or civicAddress eprofiles, we need to save the position
			 * of the first one in relation to any URI ones so we can insert the "cid"
			 * uri for it in the original position.
			 */
			if (pidf_index < 0) {
				pidf_index = i;
			}
		}
		/* The LAST eprofile determines routing */
		geoloc_routing = ep->geolocation_routing;
		ao2_ref(ep, -1);
	}

	/*
	 * If we found at least one eprofile needing PIDF processing, we'll
	 * send the entire list off to add_pidf_to_tdata().  We're going to save
	 * the pointer to the original tdata body in case we need to revert
	 * if we can't add the headers.
	 */
	orig_body = tdata->msg->body;
	if (pidf_index >= 0) {
		rc = add_pidf_to_tdata(tempds, channel, &uris, pidf_index, tdata, session_name);
	}

	/*
	 * Now that we have all the URIs in the vector, we'll string them together
	 * to create the data for the Geolocation header.
	 */
	ast_str_reset(buf);
	for (i = 0; i < AST_VECTOR_SIZE(&uris); i++) {
		char *uri = AST_VECTOR_GET(&uris, i);
		ast_trace(4, "ix: %d of %d LocRef: %s\n", i, (int)AST_VECTOR_SIZE(&uris), uri);
		ast_str_append(&buf, 0, "%s<%s>", (i > 0 ? "," : ""), uri);
	}

	AST_VECTOR_RESET(&uris, ast_free);
	AST_VECTOR_FREE(&uris);

	/* It's almost impossible for add header to fail but you never know */
	geoloc_hdr = ast_sip_add_header2(tdata, "Geolocation", ast_str_buffer(buf));
	if (geoloc_hdr == NULL) {
		tdata->msg->body = orig_body;
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Unable to add Geolocation header\n", session_name);
	}
	rc = ast_sip_add_header(tdata, "Geolocation-Routing", geoloc_routing ? "yes" : "no");
	if (rc != 0) {
		tdata->msg->body = orig_body;
		pj_list_erase(geoloc_hdr);
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Unable to add Geolocation-Routing header\n", session_name);
	}
	SCOPE_EXIT_RTN("%s: Geolocation: %s\n", session_name, ast_str_buffer(buf));
}

static struct ast_sip_session_supplement geolocation_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL + 10,
	.incoming_request = handle_incoming_request,
	.outgoing_request = handle_outgoing_request,
};

static int reload_module(void)
{
	return 0;
}

static int unload_module(void)
{
	int res = 0;
	ast_sip_session_unregister_supplement(&geolocation_supplement);

	return res;
}

static int load_module(void)
{
	int res = 0;
	GEOLOCATION_HDR = pj_str("Geolocation");

	ast_sip_session_register_supplement(&geolocation_supplement);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "res_pjsip_geolocation Module for Asterisk",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 1,
	.requires = "res_geolocation,res_pjsip,res_pjsip_session,chan_pjsip",
);
