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
static pj_str_t GEOLOCATION_ROUTING_HDR;

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

static int add_eprofile_to_channel(struct ast_sip_session *session,
	struct ast_geoloc_eprofile *eprofile, struct ast_str * buf)
{
	const char *session_name = (session ? ast_sip_session_get_name(session) : "NULL_SESSION");
	struct ast_datastore *ds = NULL;
	int rc = 0;
	SCOPE_ENTER(4, "%s\n", session_name);

	ds = ast_geoloc_datastore_create(session_name);
	if (!ds) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING,
			"%s: Couldn't allocate a geoloc datastore\n", session_name);
	}

	/*
	 * We want the datastore to pass through the dialplan and the core
	 * so we need to turn inheritance on.
	 */
	ast_geoloc_datastore_set_inheritance(ds, 1);

	rc = ast_geoloc_datastore_add_eprofile(ds, eprofile);
	if (rc <= 0) {
		ast_datastore_free(ds);
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING,
			"%s: Couldn't add eprofile '%s' to datastore\n", session_name,
			eprofile->id);
	}

	ast_channel_lock(session->channel);
	ast_channel_datastore_add(session->channel, ds);
	ast_channel_unlock(session->channel);

	SCOPE_EXIT_RTN_VALUE(0, "%s: eprofile: '%s' EffectiveLoc: %s\n",
		session_name, eprofile->id, ast_str_buffer(
		ast_variable_list_join(eprofile->effective_location, ",", "=", NULL, &buf)));
}

static int handle_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	const char *session_name = (session ? ast_sip_session_get_name(session) : "NULL_SESSION");
	struct ast_sip_endpoint *endpoint = (session ? session->endpoint : NULL);
	struct ast_channel *channel = (session ? session->channel : NULL);
	RAII_VAR(struct ast_geoloc_profile *, config_profile, NULL, ao2_cleanup);
	RAII_VAR(struct ast_geoloc_eprofile *, config_eprofile, NULL, ao2_cleanup);
	RAII_VAR(struct ast_datastore *, ds, NULL, ast_datastore_free);
	RAII_VAR(struct ast_geoloc_eprofile *, incoming_eprofile, NULL, ao2_cleanup);
	char *geoloc_hdr_value = NULL;
	char *geoloc_routing_hdr_value = NULL;
	char *geoloc_uri = NULL;
	int rc = 0;
	RAII_VAR(struct ast_str *, buf, NULL, ast_free);
	pjsip_generic_string_hdr *geoloc_hdr = NULL;
	pjsip_generic_string_hdr *geoloc_routing_hdr = NULL;
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

	/*
	 * We don't need geoloc_hdr or geoloc_routing_hdr for a while but we get it now
	 * for trace purposes.
	 */
	geoloc_hdr = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &GEOLOCATION_HDR, NULL);
	geoloc_routing_hdr = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg,
		&GEOLOCATION_ROUTING_HDR, NULL);

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
				"Done.\n", session_name,
				PJSTR_PRINTF_VAR(geoloc_hdr->hvalue));
		} else {
			SCOPE_EXIT_RTN_VALUE(0, "%s: Endpoint has no geoloc_incoming_call_profile. "
				"Done.\n", session_name);
		}
	}

	config_profile = ast_geoloc_get_profile(endpoint->geoloc_incoming_call_profile);
	if (!config_profile) {
		if (geoloc_hdr) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_NOTICE, "%s: Message has Geolocation header '"
				PJSTR_PRINTF_SPEC "' but endpoint's geoloc_incoming_call_profile doesn't exist. "
				"Done.\n", session_name,
				PJSTR_PRINTF_VAR(geoloc_hdr->hvalue));
		} else {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_NOTICE, "%s: Message has no Geolocation header and endpoint has "
				" an invalid geoloc_incoming_call_profile. Done.\n", session_name);
		}
	}

	buf = ast_str_create(1024);
	if (!buf) {
		SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING, "%s: Unable to allocate buf\n", session_name);
	}

	if (config_profile->precedence != AST_GEOLOC_PRECED_DISCARD_CONFIG) {
		config_eprofile = ast_geoloc_eprofile_create_from_profile(config_profile);
		if (!config_eprofile) {
			ast_log(LOG_WARNING, "%s: Unable to create config_eprofile from "
				"profile '%s'\n", session_name, ast_sorcery_object_get_id(config_profile));
		}

		if (config_eprofile && config_eprofile->effective_location) {
			ast_trace(4, "%s: config eprofile '%s' has effective location\n",
				session_name, config_eprofile->id);

			if (!geoloc_hdr || config_profile->precedence == AST_GEOLOC_PRECED_DISCARD_INCOMING ||
				config_profile->precedence == AST_GEOLOC_PRECED_PREFER_CONFIG) {

				ast_trace(4, "%s: config eprofile '%s' is being used\n",
					session_name, config_eprofile->id);

				/*
				 * If we have an effective location and there's no geolocation header,
				 * or the action is either DISCARD_INCOMING or PREFER_CONFIG,
				 * we don't need to even look for a Geolocation header so just add the
				 * config eprofile to the channel and exit.
				 */

				rc = add_eprofile_to_channel(session, config_eprofile, buf);
				if (rc != 0) {
					SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING,
						"%s: Couldn't add config eprofile '%s' to datastore. Fail.\n", session_name,
						config_eprofile->id);
				}

				SCOPE_EXIT_RTN_VALUE(0, "%s: Added geoloc datastore with eprofile from config. Done.\n",
					session_name);
			}
		} else {
			/*
			 * If the config eprofile has no effective location, just get rid
			 * of it.
			 */
			ast_trace(4, "%s: Either config_eprofile didn't exist or it had no effective location\n",
				session_name);

			ao2_cleanup(config_eprofile);
			config_eprofile = NULL;
			if (config_profile->precedence == AST_GEOLOC_PRECED_DISCARD_INCOMING) {
				SCOPE_EXIT_RTN_VALUE(0, "%s: DISCARD_INCOMING set and no config eprofile. Done.\n",
					session_name);
			}
		}
	}

	/*
	 * At this point, if we have a config_eprofile, then the action was
	 * PREFER_INCOMING so we're going to keep it as a backup if we can't
	 * get a profile from the incoming message.
	 */

	if (geoloc_hdr && config_profile->precedence != AST_GEOLOC_PRECED_DISCARD_INCOMING) {

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

		geoloc_hdr_value = ast_alloca(geoloc_hdr->hvalue.slen + 1);
		ast_copy_pj_str(geoloc_hdr_value, &geoloc_hdr->hvalue, geoloc_hdr->hvalue.slen + 1);

		/*
		 * We're going to scan the header value for URIs until we find
		 * one that processes successfully or we run out of URIs.
		 * I.E.  The first good one wins.
		 */
		while (geoloc_hdr_value && !incoming_eprofile) {
			char *pidf_body = NULL;
			unsigned int pidf_len = 0;
			struct ast_xml_doc *incoming_doc = NULL;
			int rc = 0;

			/* We're only going to consider the first URI in the header for now */
			geoloc_uri = ast_strsep(&geoloc_hdr_value, ',', AST_STRSEP_TRIM);
			if (ast_strlen_zero(geoloc_uri) || geoloc_uri[0] != '<' || strchr(geoloc_uri, '>') == NULL) {
				ast_log(LOG_WARNING, "%s: Geolocation header has no or bad URI '%s'.  Skipping\n", session_name,
					S_OR(geoloc_uri, "<empty>"));
				continue;
			}

			ast_trace(4, "Processing URI '%s'\n", geoloc_uri);

			if (!ast_begins_with(geoloc_uri, "<cid:")) {
				ast_trace(4, "Processing URI '%s'\n", geoloc_uri);

				incoming_eprofile = ast_geoloc_eprofile_create_from_uri(geoloc_uri, session_name);
				if (!incoming_eprofile) {
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

				incoming_eprofile = ast_geoloc_eprofile_create_from_pidf(incoming_doc, geoloc_uri, session_name);
				ast_xml_close(incoming_doc);

				if (!incoming_eprofile) {
					ast_log(LOG_WARNING,
						"%s: Couldn't create incoming_eprofile from pidf\n", session_name);
					continue;
				}
			}
		}
	}

	if (!incoming_eprofile) {
		/* Use the config_eprofile as a backup if there was one */
		incoming_eprofile = config_eprofile;
	} else {
		ao2_cleanup(config_eprofile);
		config_eprofile = NULL;
		if (geoloc_routing_hdr) {
			geoloc_routing_hdr_value = ast_alloca(geoloc_routing_hdr->hvalue.slen + 1);
			ast_copy_pj_str(geoloc_routing_hdr_value, &geoloc_routing_hdr->hvalue,
				geoloc_routing_hdr->hvalue.slen + 1);
			incoming_eprofile->allow_routing_use = ast_true(geoloc_routing_hdr_value);
		}
	}

	if (incoming_eprofile) {
		rc = add_eprofile_to_channel(session, incoming_eprofile, buf);
		if (rc != 0) {
			SCOPE_EXIT_LOG_RTN_VALUE(0, LOG_WARNING,
				"%s: Couldn't add eprofile '%s' to channel. Fail.\n", session_name,
				incoming_eprofile->id);
		}

		SCOPE_EXIT_RTN_VALUE(0, "%s: Added eprofile '%s' to channel. Done.\n",
			session_name, incoming_eprofile->id);
	}

	SCOPE_EXIT_RTN_VALUE(0, "%s: No eprofiles to add to channel. Done.\n",	session_name);
}

static const char *add_eprofile_to_tdata(struct ast_geoloc_eprofile *eprofile, struct ast_channel *channel,
	struct pjsip_tx_data *tdata, struct ast_str **buf, const char *session_name)
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
	const char *final_doc;
	int rc = 0;
	SCOPE_ENTER(3, "%s\n", session_name);

	/*
	 * ast_geoloc_eprofiles_to_pidf() takes the datastore with all of the eprofiles
	 * in it, skips over the ones not needing PIDF processing and combines the
	 * rest into one document.
	 */
	final_doc = ast_geoloc_eprofile_to_pidf(eprofile, channel, buf, session_name);
	ast_trace(5, "Final pidf: \n%s\n", final_doc);

	if (!final_doc) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create pidf document from"
			" eprofile '%s'\n\n", session_name, eprofile->id);
	}

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
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create sdp multipart body\n",
				session_name);
		}
	} else {
	    multipart_body = pjsip_multipart_create(tdata->pool, &pjsip_media_type_multipart_mixed, NULL);
	}

	pidf_part = pjsip_multipart_create_part(tdata->pool);
	pj_cstr(&pidf_body_text, final_doc);
	pidf_part->body = pjsip_msg_body_create(tdata->pool, &pjsip_media_type_application_pidf_xml.type,
		&pjsip_media_type_application_pidf_xml.subtype, &pidf_body_text);

    pjsip_multipart_add_part(tdata->pool, multipart_body, pidf_part);

	sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(from->uri);
	alloc_size = sizeof(id) + pj_strlen(&sip_uri->host) + 2;
	base_cid = ast_malloc(alloc_size);
	sprintf(base_cid, "%s@%.*s",
			ast_generate_random_string(id, sizeof(id)),
			(int) pj_strlen(&sip_uri->host), pj_strbuf(&sip_uri->host));

	ast_str_set(buf, 0, "cid:%s", base_cid);
	ast_trace(4, "cid: '%s' uri: '%s'\n", base_cid, ast_str_buffer(*buf));

	cid_value.ptr = pj_pool_alloc(tdata->pool, alloc_size);
	cid_value.slen = sprintf(cid_value.ptr, "<%s>", base_cid);

	cid = pjsip_generic_string_hdr_create(tdata->pool, &cid_name, &cid_value);

	pj_list_insert_after(&pidf_part->hdr, cid);

    tdata->msg->body = multipart_body;

	SCOPE_EXIT_RTN_VALUE(ast_str_buffer(*buf), "%s: PIDF-LO added with cid '%s'\n", session_name, base_cid);
}

static void handle_outgoing_request(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	const char *session_name = ast_sip_session_get_name(session);
	struct ast_sip_endpoint *endpoint = session->endpoint;
	struct ast_channel *channel = session->channel;
	RAII_VAR(struct ast_geoloc_profile *, config_profile, NULL, ao2_cleanup);
	RAII_VAR(struct ast_geoloc_eprofile *, config_eprofile, NULL, ao2_cleanup);
	RAII_VAR(struct ast_geoloc_eprofile *, incoming_eprofile, NULL, ao2_cleanup);
	struct ast_geoloc_eprofile *final_eprofile = NULL;
	RAII_VAR(struct ast_str *, buf, NULL, ast_free);
	struct ast_datastore *ds = NULL;  /* The channel cleans up ds */
	pjsip_msg_body *orig_body = NULL;
	pjsip_generic_string_hdr *geoloc_hdr = NULL;
	int eprofile_count = 0;
	int rc = 0;
	const char *uri;
	SCOPE_ENTER(3, "%s\n", session_name);

	if (!endpoint) {
		SCOPE_EXIT_LOG_RTN(LOG_WARNING, "%s: Session has no endpoint.  Skipping.\n",
			session_name);
	}

	if (!channel) {
		SCOPE_EXIT_LOG_RTN(LOG_WARNING, "%s: Session has no channel.  Skipping.\n",
			session_name);
	}

	if (ast_strlen_zero(endpoint->geoloc_outgoing_call_profile)) {
		SCOPE_EXIT_RTN("%s: Endpoint has no geoloc_outgoing_call_profile. Skipping.\n",
			session_name);
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

	if (!config_eprofile->effective_location) {
		/*
		 * If there's no effective location on the eprofile
		 * we don't need to keep it.
		 */
		ast_trace(4, "%s: There was no effective location for config profile '%s'\n",
			session_name, ast_sorcery_object_get_id(config_profile));
		ao2_ref(config_eprofile, -1);
		config_eprofile = NULL;
	}

	ds = ast_geoloc_datastore_find(channel);
	if (!ds) {
		ast_trace(4, "%s: There was no geoloc datastore on the channel\n", session_name);
	} else {
		eprofile_count = ast_geoloc_datastore_size(ds);
		ast_trace(4, "%s: There are %d geoloc profiles on this channel\n", session_name,
			eprofile_count);
		/*
		 * There'd better be a max of 1 at this time.  In the future
		 * we may allow more than 1.
		 */
		incoming_eprofile = ast_geoloc_datastore_get_eprofile(ds, 0);
	}

	ast_trace(4, "%s: Profile precedence: %s\n\n", session_name,
		ast_geoloc_precedence_to_name(config_profile->precedence));

	switch (config_profile->precedence) {
	case AST_GEOLOC_PRECED_DISCARD_INCOMING:
		final_eprofile = config_eprofile;
		ao2_cleanup(incoming_eprofile);
		incoming_eprofile = NULL;
		break;
	case AST_GEOLOC_PRECED_PREFER_INCOMING:
		if (incoming_eprofile) {
			final_eprofile = incoming_eprofile;
			ao2_cleanup(config_eprofile);
			config_eprofile = NULL;
		} else {
			final_eprofile = config_eprofile;
		}
		break;
	case AST_GEOLOC_PRECED_DISCARD_CONFIG:
		final_eprofile = incoming_eprofile;
		ao2_cleanup(config_eprofile);
		config_eprofile = NULL;
		break;
	case AST_GEOLOC_PRECED_PREFER_CONFIG:
		if (config_eprofile) {
			final_eprofile = config_eprofile;
			ao2_cleanup(incoming_eprofile);
			incoming_eprofile = NULL;
		} else {
			final_eprofile = incoming_eprofile;
		}
		break;
	}

	if (!final_eprofile) {
		SCOPE_EXIT_RTN("%s: No eprofiles to send.  Done.\n",
			session_name);
	}

	if (!final_eprofile->effective_location) {
		ast_geoloc_eprofile_refresh_location(final_eprofile);
	}

	buf = ast_str_create(1024);
	if (!buf) {
		SCOPE_EXIT_LOG_RTN(LOG_WARNING, "%s: Unable to allocate buf\n", session_name);
	}

	if (final_eprofile->format == AST_GEOLOC_FORMAT_URI) {
		uri = ast_geoloc_eprofile_to_uri(final_eprofile, channel, &buf, session_name);
		if (!uri) {
			SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Unable to create URI from eprofile '%s'\n",
				session_name, final_eprofile->id);
		}
	} else {
		orig_body = tdata->msg->body;
		uri = add_eprofile_to_tdata(final_eprofile, channel, tdata, &buf, session_name);
		if (!uri) {
			tdata->msg->body = orig_body;
			SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Unable to add eprofile '%s' to tdata\n",
				session_name, final_eprofile->id);
		}
	}

	uri = ast_strdupa(ast_str_buffer(buf));
	ast_str_reset(buf);
	ast_str_set(&buf, 0, "<%s>", uri);
	uri = ast_strdupa(ast_str_buffer(buf));

	ast_trace(4, "%s: Using URI '%s'\n", session_name, uri);

	/* It's almost impossible for add header to fail but you never know */
	geoloc_hdr = ast_sip_add_header2(tdata, "Geolocation", uri);
	if (geoloc_hdr == NULL) {
		if (orig_body) {
			tdata->msg->body = orig_body;
		}
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Unable to add Geolocation header\n", session_name);
	}
	rc = ast_sip_add_header(tdata, "Geolocation-Routing", final_eprofile->allow_routing_use ? "yes" : "no");
	if (rc != 0) {
		if (orig_body) {
			tdata->msg->body = orig_body;
		}
		pj_list_erase(geoloc_hdr);
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Unable to add Geolocation-Routing header\n", session_name);
	}
	SCOPE_EXIT_RTN("%s: Geolocation: %s\n", session_name, uri);
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
	GEOLOCATION_ROUTING_HDR = pj_str("Geolocation-Routing");

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
