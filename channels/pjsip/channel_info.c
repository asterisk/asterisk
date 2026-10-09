/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sangoma Technologies Corporation
 *
 * Michael Bradeen <mbradeen@sangoma.com>
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

/*!
 * \file
 * \brief Channel-locked PJSIP information for CHANNEL() reads
 *
 * The channel datastore owns cached dialog URIs, the remote tag, signaling
 * security, RTP instance references, and media security and hold flags.
 * Readers and writers access the cache under the channel lock. A datastore
 * lookup returns a borrowed pointer valid only while that lock is held.
 *
 * Session serializer tasks refresh the cache through session->channel.
 * SIP supplements refresh it during incoming message processing and queue
 * refreshes for subsequent changes. Masquerades suspend the serializer while
 * changing the channel association and move the datastore with the channel.
 *
 * CHANNEL() reads use the cache for values that SIP and SDP processing can
 * change without the channel lock. Stable identity and original request
 * information are read directly from the session. Direct-media addresses are
 * read under the channel lock. RTP APIs supply RTP addresses and statistics.
 *
 * These reads do not wait for the session serializer or acquire the dialog
 * lock while holding the channel lock to avoid a deadlock with masquerade
 * processing.
 *
 */

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/astobj2.h"
#include "asterisk/channel.h"
#include "asterisk/datastore.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/rtp_engine.h"

#include "include/channel_info.h"

static void channel_info_destroy(void *data)
{
	struct pjsip_channel_info *info = data;

	ast_free(info->remote_tag);
	ao2_cleanup(info->audio_rtp);
	ao2_cleanup(info->video_rtp);
	ast_free(info);
}

static void channel_info_breakdown(void *data, struct ast_channel *old_chan,
	struct ast_channel *new_chan)
{
	struct pjsip_channel_info *info = data;
	struct ast_datastore *datastore = info->datastore;

	/*
	 * Core calls breakdown on the destination's old datastores before moving
	 * the source's datastores over. Both channels are locked. The moved
	 * datastore needs no fixup because it contains no channel pointer.
	 */
	if (!ast_channel_datastore_remove(new_chan, datastore)) {
		ast_datastore_free(datastore);
	}
}

static const struct ast_datastore_info channel_info_datastore = {
	.type = "chan_pjsip_channel_info",
	.destroy = channel_info_destroy,
	.chan_breakdown = channel_info_breakdown,
};

struct pjsip_channel_info *pjsip_channel_info_get(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	datastore = ast_channel_datastore_find(chan, &channel_info_datastore, NULL);
	return datastore ? datastore->data : NULL;
}

/* Return whether the value changed, preserving print failures for the reader. */
static int copy_uri(struct pjsip_channel_uri *dest, pjsip_uri_context_e context,
	const void *uri, const char *type)
{
	char value[PJSIP_MAX_URL_SIZE] = "";
	int length = 0;
	int changed;

	if (uri) {
		length = pjsip_uri_print(context, uri, value, sizeof(value));
		if (length < 0 || length >= sizeof(value)) {
			ast_log(LOG_ERROR, "Unable to capture PJSIP %s in %zu bytes\n",
				type, sizeof(value));
			value[0] = '\0';
			length = -1;
		} else {
			value[length] = '\0';
		}
	}
	changed = dest->length != length || strcmp(dest->value, value);
	ast_copy_string(dest->value, value, sizeof(dest->value));
	dest->length = length;
	return changed;
}

static void channel_info_update_locked(struct pjsip_channel_info *info,
	struct ast_sip_session *session)
{
	pjsip_dialog *dlg = session->inv_session ? session->inv_session->dlg : NULL;
	struct ast_sip_session_media *media;

	if (dlg) {
		int target_changed;
		const pj_str_t *tag = &dlg->remote.info->tag;

		target_changed = copy_uri(&info->target_uri, PJSIP_URI_IN_REQ_URI,
			dlg->target, "target_uri");
		copy_uri(&info->local_uri, PJSIP_URI_IN_FROMTO_HDR,
			dlg->local.info->uri, "local_uri");
		copy_uri(&info->remote_uri, PJSIP_URI_IN_FROMTO_HDR,
			dlg->remote.info->uri, "remote_uri");
		/* PJSIP_MAX_TAG_LEN describes generated tags, not a received-tag limit. */
		if (!info->remote_tag || strlen(info->remote_tag) != tag->slen
			|| (tag->slen && memcmp(info->remote_tag, tag->ptr, tag->slen))) {
			char *copy = ast_strndup(tag->slen ? tag->ptr : "", tag->slen);

			ast_free(info->remote_tag);
			info->remote_tag = copy;
		}
#ifdef HAVE_PJSIP_GET_DEST_INFO
		if (target_changed || info->target_uri.length < 0 || info->secure < 0) {
			pjsip_host_info dest;
			pj_pool_t *pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(),
				"channel-info", 128, 128);

			info->secure = -1;
			if (pool) {
				if (pjsip_get_dest_info(dlg->target, NULL, pool, &dest) == PJ_SUCCESS) {
					info->secure = !!(dest.flag & PJSIP_TRANSPORT_SECURE);
				}
				pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
			}
		}
#else
		ast_log(LOG_WARNING, "Asterisk has been built against a version of pjproject which does not have the required functionality to support the 'secure' argument. Please upgrade to version 2.3 or later.\n");
		(void) target_changed;
#endif
	} else {
		copy_uri(&info->target_uri, PJSIP_URI_IN_REQ_URI, NULL, "target_uri");
		copy_uri(&info->local_uri, PJSIP_URI_IN_FROMTO_HDR, NULL, "local_uri");
		copy_uri(&info->remote_uri, PJSIP_URI_IN_FROMTO_HDR, NULL, "remote_uri");
		ast_free(info->remote_tag);
		info->remote_tag = NULL;
		info->secure = -1;
	}

	media = session->active_media_state
		? session->active_media_state->default_session[AST_MEDIA_TYPE_AUDIO] : NULL;
	ao2_replace(info->audio_rtp, media ? media->rtp : NULL);
	info->audio_secure = media && media->srtp
		&& ast_test_flag(media->srtp, AST_SRTP_CRYPTO_OFFER_OK);
	info->audio_held = media && media->remotely_held;
	media = session->active_media_state
		? session->active_media_state->default_session[AST_MEDIA_TYPE_VIDEO] : NULL;
	ao2_replace(info->video_rtp, media ? media->rtp : NULL);
	info->video_secure = media && media->srtp
		&& ast_test_flag(media->srtp, AST_SRTP_CRYPTO_OFFER_OK);
	info->video_held = media && media->remotely_held;
}

int pjsip_channel_info_create(struct ast_channel *chan,
	struct ast_sip_session *session)
{
	struct ast_datastore *datastore;
	struct pjsip_channel_info *info;

	datastore = ast_datastore_alloc(&channel_info_datastore, NULL);
	if (!datastore) {
		return -1;
	}
	info = ast_calloc(1, sizeof(*info));
	if (!info) {
		ast_datastore_free(datastore);
		return -1;
	}
	info->datastore = datastore;
	info->secure = -1;
	datastore->data = info;

	/* The caller holds the channel lock and is running in the serializer. */
	channel_info_update_locked(info, session);
	ast_channel_datastore_add(chan, datastore);
	return 0;
}

void pjsip_channel_info_update(struct ast_sip_session *session)
{
	struct ast_channel *chan = session->channel;
	struct pjsip_channel_info *info;

	/* Serializer execution stabilizes session->channel across a masquerade. */
	if (!chan) {
		return;
	}
	ast_channel_lock(chan);
	info = pjsip_channel_info_get(chan);
	if (info) {
		channel_info_update_locked(info, session);
	}
	ast_channel_unlock(chan);
}

static int channel_info_update_task(void *data)
{
	struct ast_sip_session *session = data;

	pjsip_channel_info_update(session);
	ao2_ref(session, -1);
	return 0;
}

void pjsip_channel_info_schedule_update(struct ast_sip_session *session)
{
	/*
	 * Capture changes made after supplements or from an outgoing transport
	 * callback outside the serializer. Never wait for the queued refresh.
	 */
	if (ast_sip_push_task(session->serializer, channel_info_update_task,
		ao2_bump(session))) {
		ast_log(LOG_WARNING, "Unable to queue PJSIP channel information refresh for %s\n",
			ast_sip_session_get_name(session));
		ao2_ref(session, -1);
	}
}

static int channel_info_incoming_request(struct ast_sip_session *session,
	struct pjsip_rx_data *rdata)
{
	pjsip_channel_info_update(session);
	pjsip_channel_info_schedule_update(session);
	return 0;
}

static void channel_info_incoming_response(struct ast_sip_session *session,
	struct pjsip_rx_data *rdata)
{
	pjsip_channel_info_update(session);
	pjsip_channel_info_schedule_update(session);
}

static void channel_info_outgoing_request(struct ast_sip_session *session,
	struct pjsip_tx_data *tdata)
{
	pjsip_channel_info_schedule_update(session);
}

static void channel_info_outgoing_response(struct ast_sip_session *session,
	struct pjsip_tx_data *tdata)
{
	pjsip_channel_info_schedule_update(session);
}

static struct ast_sip_session_supplement channel_info_supplement = {
	/* Refresh before channel supplements publish snapshots or queue control frames. */
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL - 1,
	.incoming_request = channel_info_incoming_request,
	.incoming_response = channel_info_incoming_response,
	.outgoing_request = channel_info_outgoing_request,
	.outgoing_response = channel_info_outgoing_response,
	.response_priority = AST_SIP_SESSION_AFTER_MEDIA,
};

void pjsip_channel_info_register(void)
{
	ast_sip_session_register_supplement(&channel_info_supplement);
}

void pjsip_channel_info_unregister(void)
{
	ast_sip_session_unregister_supplement(&channel_info_supplement);
}
