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

/*! \file
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 * \brief Gulp SIP Channel Driver
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_sip</depend>
	<depend>res_sip_session</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>
#include <pjlib.h>

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/musiconhold.h"
#include "asterisk/causes.h"
#include "asterisk/taskprocessor.h"

#include "asterisk/res_sip.h"
#include "asterisk/res_sip_session.h"

/*** DOCUMENTATION
	<function name="GULP_DIAL_CONTACTS" language="en_US">
		<synopsis>
			Return a dial string for dialing all contacts on an AOR.
		</synopsis>
		<syntax>
			<parameter name="endpoint" required="true">
				<para>Name of the endpoint</para>
			</parameter>
			<parameter name="aor" required="false">
				<para>Name of an AOR to use, if not specified the configured AORs on the endpoint are used</para>
			</parameter>
			<parameter name="request_user" required="false">
				<para>Optional request user to use in the request URI</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns a properly formatted dial string for dialing all contacts on an AOR.</para>
		</description>
	</function>
 ***/

static const char desc[] = "Gulp SIP Channel";
static const char channel_type[] = "Gulp";

/*!
 * \brief Positions of various media
 */
enum sip_session_media_position {
	/*! \brief First is audio */
	SIP_MEDIA_AUDIO = 0,
	/*! \brief Second is video */
	SIP_MEDIA_VIDEO,
	/*! \brief Last is the size for media details */
	SIP_MEDIA_SIZE,
};

struct gulp_pvt {
	struct ast_sip_session *session;
	struct ast_sip_session_media *media[SIP_MEDIA_SIZE];
};

static void gulp_pvt_dtor(void *obj)
{
	struct gulp_pvt *pvt = obj;
	int i;
	ao2_cleanup(pvt->session);
	pvt->session = NULL;
	for (i = 0; i < SIP_MEDIA_SIZE; ++i) {
		ao2_cleanup(pvt->media[i]);
		pvt->media[i] = NULL;
	}
}

/* \brief Asterisk core interaction functions */
static struct ast_channel *gulp_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause);
static int gulp_sendtext(struct ast_channel *ast, const char *text);
static int gulp_digit_begin(struct ast_channel *ast, char digit);
static int gulp_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int gulp_call(struct ast_channel *ast, const char *dest, int timeout);
static int gulp_hangup(struct ast_channel *ast);
static int gulp_answer(struct ast_channel *ast);
static struct ast_frame *gulp_read(struct ast_channel *ast);
static int gulp_write(struct ast_channel *ast, struct ast_frame *f);
static int gulp_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);
static int gulp_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);

/*! \brief PBX interface structure for channel registration */
static struct ast_channel_tech gulp_tech = {
	.type = channel_type,
	.description = "Gulp SIP Channel Driver",
	.requester = gulp_request,
	.send_text = gulp_sendtext,
	.send_digit_begin = gulp_digit_begin,
	.send_digit_end = gulp_digit_end,
	.bridge = ast_rtp_instance_bridge,
	.call = gulp_call,
	.hangup = gulp_hangup,
	.answer = gulp_answer,
	.read = gulp_read,
	.write = gulp_write,
	.write_video = gulp_write,
	.exception = gulp_read,
	.indicate = gulp_indicate,
	.fixup = gulp_fixup,
	.properties = AST_CHAN_TP_WANTSJITTER | AST_CHAN_TP_CREATESJITTER
};

/*! \brief SIP session interaction functions */
static void gulp_session_begin(struct ast_sip_session *session);
static void gulp_session_end(struct ast_sip_session *session);
static int gulp_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata);
static void gulp_incoming_response(struct ast_sip_session *session, struct pjsip_rx_data *rdata);

/*! \brief SIP session supplement structure */
static struct ast_sip_session_supplement gulp_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SESSION_SUPPLEMENT_PRIORITY_CHANNEL,
	.session_begin = gulp_session_begin,
	.session_end = gulp_session_end,
	.incoming_request = gulp_incoming_request,
	.incoming_response = gulp_incoming_response,
};

static int gulp_incoming_ack(struct ast_sip_session *session, struct pjsip_rx_data *rdata);

static struct ast_sip_session_supplement gulp_ack_supplement = {
	.method = "ACK",
	.priority = AST_SIP_SESSION_SUPPLEMENT_PRIORITY_CHANNEL,
	.incoming_request = gulp_incoming_ack,
};

/*! \brief Dialplan function for constructing a dial string for calling all contacts */
static int gulp_dial_contacts(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(endpoint_name);
		AST_APP_ARG(aor_name);
		AST_APP_ARG(request_user);
	);
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	const char *aor_name;
	char *rest;
	RAII_VAR(struct ast_str *, dial, NULL, ast_free_ptr);

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.endpoint_name)) {
		ast_log(LOG_WARNING, "An endpoint name must be specified when using the '%s' dialplan function\n", cmd);
		return -1;
	} else if (!(endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", args.endpoint_name))) {
		ast_log(LOG_WARNING, "Specified endpoint '%s' was not found\n", args.endpoint_name);
		return -1;
	}

	aor_name = S_OR(args.aor_name, endpoint->aors);

	if (ast_strlen_zero(aor_name)) {
		ast_log(LOG_WARNING, "No AOR has been provided and no AORs are configured on endpoint '%s'\n", args.endpoint_name);
		return -1;
	} else if (!(dial = ast_str_create(len))) {
		ast_log(LOG_WARNING, "Could not get enough buffer space for dialing contacts\n");
		return -1;
	} else if (!(rest = ast_strdupa(aor_name))) {
		ast_log(LOG_WARNING, "Could not duplicate provided AORs\n");
		return -1;
	}

	while ((aor_name = strsep(&rest, ","))) {
		RAII_VAR(struct ast_sip_aor *, aor, ast_sip_location_retrieve_aor(aor_name), ao2_cleanup);
		RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);
		struct ao2_iterator it_contacts;
		struct ast_sip_contact *contact;

		if (!aor) {
			/* If the AOR provided is not found skip it, there may be more */
			continue;
		} else if (!(contacts = ast_sip_location_retrieve_aor_contacts(aor))) {
			/* No contacts are available, skip it as well */
			continue;
		} else if (!ao2_container_count(contacts)) {
			/* We were given a container but no contacts are in it... */
			continue;
		}

		it_contacts = ao2_iterator_init(contacts, 0);
		for (; (contact = ao2_iterator_next(&it_contacts)); ao2_ref(contact, -1)) {
			ast_str_append(&dial, -1, "Gulp/");

			if (!ast_strlen_zero(args.request_user)) {
				ast_str_append(&dial, -1, "%s@", args.request_user);
			}
			ast_str_append(&dial, -1, "%s/%s&", args.endpoint_name, contact->uri);
		}
		ao2_iterator_destroy(&it_contacts);
	}

	/* Trim the '&' at the end off */
	ast_str_truncate(dial, ast_str_strlen(dial) - 1);

	ast_copy_string(buf, ast_str_buffer(dial), len);

	return 0;
}

static struct ast_custom_function gulp_dial_contacts_function = {
	.name = "GULP_DIAL_CONTACTS",
	.read = gulp_dial_contacts,
};

/*! \brief Function called by RTP engine to get local audio RTP peer */
static enum ast_rtp_glue_result gulp_get_rtp_peer(struct ast_channel *chan, struct ast_rtp_instance **instance)
{
	struct gulp_pvt *pvt = ast_channel_tech_pvt(chan);
	struct ast_sip_endpoint *endpoint;

	if (!pvt || !pvt->session || !pvt->media[SIP_MEDIA_AUDIO]->rtp) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	endpoint = pvt->session->endpoint;

	*instance = pvt->media[SIP_MEDIA_AUDIO]->rtp;
	ao2_ref(*instance, +1);

	ast_assert(endpoint != NULL);
	if (endpoint->direct_media) {
		return AST_RTP_GLUE_RESULT_REMOTE;
	}

	return AST_RTP_GLUE_RESULT_LOCAL;
}

/*! \brief Function called by RTP engine to get local video RTP peer */
static enum ast_rtp_glue_result gulp_get_vrtp_peer(struct ast_channel *chan, struct ast_rtp_instance **instance)
{
	struct gulp_pvt *pvt = ast_channel_tech_pvt(chan);

	if (!pvt || !pvt->session || !pvt->media[SIP_MEDIA_VIDEO]->rtp) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	*instance = pvt->media[SIP_MEDIA_VIDEO]->rtp;
	ao2_ref(*instance, +1);

	return AST_RTP_GLUE_RESULT_LOCAL;
}

/*! \brief Function called by RTP engine to get peer capabilities */
static void gulp_get_codec(struct ast_channel *chan, struct ast_format_cap *result)
{
	struct gulp_pvt *pvt = ast_channel_tech_pvt(chan);
	ast_format_cap_copy(result, pvt->session->endpoint->codecs);
}

static int send_direct_media_request(void *data)
{
	RAII_VAR(struct ast_sip_session *, session, data, ao2_cleanup);
	return ast_sip_session_refresh(session, NULL, NULL, session->endpoint->direct_media_method, 1);
}

static struct ast_datastore_info direct_media_mitigation_info = { };

static int direct_media_mitigate_glare(struct ast_sip_session *session)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);

	if (session->endpoint->direct_media_glare_mitigation == 
			AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_NONE) {
		return 0;
	}

	datastore = ast_sip_session_get_datastore(session, "direct_media_glare_mitigation");
	if (!datastore) {
		return 0;
	}

	/* Removing the datastore ensures we won't try to mitigate glare on subsequent reinvites */
	ast_sip_session_remove_datastore(session, "direct_media_glare_mitigation");

	if ((session->endpoint->direct_media_glare_mitigation ==
			AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_OUTGOING &&
			session->inv_session->role == PJSIP_ROLE_UAC) ||
			(session->endpoint->direct_media_glare_mitigation ==
			AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_INCOMING &&
			session->inv_session->role == PJSIP_ROLE_UAS)) {
		return 1;
	}

	return 0;
}

static int check_for_rtp_changes(struct ast_channel *chan, struct ast_rtp_instance *rtp,
		struct ast_sip_session_media *media, int rtcp_fd)
{
	int changed = 0;

	if (rtp) {
		changed = ast_rtp_instance_get_and_cmp_remote_address(rtp, &media->direct_media_addr);
		if (media->rtp) {
			ast_channel_set_fd(chan, rtcp_fd, -1);
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_RTCP, 0);
		}
	} else if (!ast_sockaddr_isnull(&media->direct_media_addr)){
		ast_sockaddr_setnull(&media->direct_media_addr);
		changed = 1;
		if (media->rtp) {
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_RTCP, 1);
			ast_channel_set_fd(chan, rtcp_fd, ast_rtp_instance_fd(media->rtp, 1));
		}
	}

	return changed;
}

/*! \brief Function called by RTP engine to change where the remote party should send media */
static int gulp_set_rtp_peer(struct ast_channel *chan,
		struct ast_rtp_instance *rtp,
		struct ast_rtp_instance *vrtp,
		struct ast_rtp_instance *tpeer,
		const struct ast_format_cap *cap,
		int nat_active)
{
	struct gulp_pvt *pvt = ast_channel_tech_pvt(chan);
	struct ast_sip_session *session = pvt->session;
	int changed = 0;

	/* Don't try to do any direct media shenanigans on early bridges */
	if ((rtp || vrtp || tpeer) && !ast_bridged_channel(chan)) {
		return 0;
	}

	if (nat_active && session->endpoint->disable_direct_media_on_nat) {
		return 0;
	}

	if (pvt->media[SIP_MEDIA_AUDIO]) {
		changed |= check_for_rtp_changes(chan, rtp, pvt->media[SIP_MEDIA_AUDIO], 1);
	}
	if (pvt->media[SIP_MEDIA_VIDEO]) {
		changed |= check_for_rtp_changes(chan, vrtp, pvt->media[SIP_MEDIA_VIDEO], 3);
	}

	if (direct_media_mitigate_glare(session)) {
		return 0;
	}

	if (cap && !ast_format_cap_is_empty(cap) && !ast_format_cap_identical(session->direct_media_cap, cap)) {
		ast_format_cap_copy(session->direct_media_cap, cap);
		changed = 1;
	}

	if (changed) {
		ao2_ref(session, +1);
		ast_sip_push_task(session->serializer, send_direct_media_request, session);
	}

	return 0;
}

/*! \brief Local glue for interacting with the RTP engine core */
static struct ast_rtp_glue gulp_rtp_glue = {
	.type = "Gulp",
	.get_rtp_info = gulp_get_rtp_peer,
	.get_vrtp_info = gulp_get_vrtp_peer,
	.get_codec = gulp_get_codec,
	.update_peer = gulp_set_rtp_peer,
};

/*! \brief Function called to create a new Gulp Asterisk channel */
static struct ast_channel *gulp_new(struct ast_sip_session *session, int state, const char *exten, const char *title, const char *linkedid, const char *cid_name)
{
	struct ast_channel *chan;
	struct ast_format fmt;
	struct gulp_pvt *pvt;

	if (!(pvt = ao2_alloc(sizeof(*pvt), gulp_pvt_dtor))) {
		return NULL;
	}

	if (!(chan = ast_channel_alloc(1, state, S_OR(session->id.number.str, ""), S_OR(session->id.name.str, ""), "", "", "", linkedid, 0, "Gulp/%s-%.*s", ast_sorcery_object_get_id(session->endpoint),
		(int)session->inv_session->dlg->call_id->id.slen, session->inv_session->dlg->call_id->id.ptr))) {
		ao2_cleanup(pvt);
		return NULL;
	}

	ast_channel_tech_set(chan, &gulp_tech);

	ao2_ref(session, +1);
	pvt->session = session;
	/* If res_sip_session is ever updated to create/destroy ast_sip_session_media
	 * during a call such as if multiple same-type stream support is introduced,
	 * these will need to be recaptured as well */
	pvt->media[SIP_MEDIA_AUDIO] = ao2_find(session->media, "audio", OBJ_KEY);
	pvt->media[SIP_MEDIA_VIDEO] = ao2_find(session->media, "video", OBJ_KEY);
	ast_channel_tech_pvt_set(chan, pvt);

	if (ast_format_cap_is_empty(session->req_caps) || !ast_format_cap_has_joint(session->req_caps, session->endpoint->codecs)) {
		ast_format_cap_copy(ast_channel_nativeformats(chan), session->endpoint->codecs);
	} else {
		ast_format_cap_copy(ast_channel_nativeformats(chan), session->req_caps);
	}

	ast_codec_choose(&session->endpoint->prefs, ast_channel_nativeformats(chan), 1, &fmt);
	ast_format_copy(ast_channel_writeformat(chan), &fmt);
	ast_format_copy(ast_channel_rawwriteformat(chan), &fmt);
	ast_format_copy(ast_channel_readformat(chan), &fmt);
	ast_format_copy(ast_channel_rawreadformat(chan), &fmt);

	if (state == AST_STATE_RING) {
		ast_channel_rings_set(chan, 1);
	}

	ast_channel_adsicpe_set(chan, AST_ADSI_UNAVAILABLE);

	ast_channel_context_set(chan, session->endpoint->context);
	ast_channel_exten_set(chan, S_OR(exten, "s"));
	ast_channel_priority_set(chan, 1);

	return chan;
}

static int answer(void *data)
{
	pj_status_t status;
	pjsip_tx_data *packet;
	struct ast_sip_session *session = data;

	if ((status = pjsip_inv_answer(session->inv_session, 200, NULL, NULL, &packet)) == PJ_SUCCESS) {
		ast_sip_session_send_response(session, packet);
	}

	ao2_ref(session, -1);
	return (status == PJ_SUCCESS) ? 0 : -1;
}

/*! \brief Function called by core when we should answer a Gulp session */
static int gulp_answer(struct ast_channel *ast)
{
	struct gulp_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ast_sip_session *session = pvt->session;

	if (ast_channel_state(ast) == AST_STATE_UP) {
		return 0;
	}

	ast_setstate(ast, AST_STATE_UP);

	ao2_ref(session, +1);
	if (ast_sip_push_task(session->serializer, answer, session)) {
		ast_log(LOG_WARNING, "Unable to push answer task to the threadpool. Cannot answer call\n");
		ao2_cleanup(session);
		return -1;
	}
	return 0;
}

/*! \brief Function called by core to read any waiting frames */
static struct ast_frame *gulp_read(struct ast_channel *ast)
{
	struct gulp_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ast_frame *f;
	struct ast_sip_session_media *media = NULL;
	int rtcp = 0;
	int fdno = ast_channel_fdno(ast);

	switch (fdno) {
	case 0:
		media = pvt->media[SIP_MEDIA_AUDIO];
		break;
	case 1:
		media = pvt->media[SIP_MEDIA_AUDIO];
		rtcp = 1;
		break;
	case 2:
		media = pvt->media[SIP_MEDIA_VIDEO];
		break;
	case 3:
		media = pvt->media[SIP_MEDIA_VIDEO];
		rtcp = 1;
		break;
	}

	if (!media || !media->rtp) {
		return &ast_null_frame;
	}

	f = ast_rtp_instance_read(media->rtp, rtcp);

	if (f && f->frametype == AST_FRAME_VOICE) {
		if (!(ast_format_cap_iscompatible(ast_channel_nativeformats(ast), &f->subclass.format))) {
			ast_debug(1, "Oooh, format changed to %s\n", ast_getformatname(&f->subclass.format));
			ast_format_cap_set(ast_channel_nativeformats(ast), &f->subclass.format);
			ast_set_read_format(ast, ast_channel_readformat(ast));
			ast_set_write_format(ast, ast_channel_writeformat(ast));
		}
	}

	return f;
}

/*! \brief Function called by core to write frames */
static int gulp_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct gulp_pvt *pvt = ast_channel_tech_pvt(ast);
	int res = 0;
	struct ast_sip_session_media *media;

	switch (frame->frametype) {
	case AST_FRAME_VOICE:
		media = pvt->media[SIP_MEDIA_AUDIO];

		if (!media) {
			return 0;
		}
		if (!(ast_format_cap_iscompatible(ast_channel_nativeformats(ast), &frame->subclass.format))) {
			char buf[256];

			ast_log(LOG_WARNING,
				"Asked to transmit frame type %s, while native formats is %s (read/write = %s/%s)\n",
				ast_getformatname(&frame->subclass.format),
				ast_getformatname_multiple(buf, sizeof(buf), ast_channel_nativeformats(ast)),
				ast_getformatname(ast_channel_readformat(ast)),
				ast_getformatname(ast_channel_writeformat(ast)));
			return 0;
		}
		if (media->rtp) {
			res = ast_rtp_instance_write(media->rtp, frame);
		}
		break;
	case AST_FRAME_VIDEO:
		if ((media = pvt->media[SIP_MEDIA_VIDEO]) && media->rtp) {
			res = ast_rtp_instance_write(media->rtp, frame);
		}
		break;
	default:
		ast_log(LOG_WARNING, "Can't send %d type frames with Gulp\n", frame->frametype);
		break;
	}

	return res;
}

struct fixup_data {
	struct ast_sip_session *session;
	struct ast_channel *chan;
};

static int fixup(void *data)
{
	struct fixup_data *fix_data = data;
	fix_data->session->channel = fix_data->chan;
	return 0;
}

/*! \brief Function called by core to change the underlying owner channel */
static int gulp_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct gulp_pvt *pvt = ast_channel_tech_pvt(newchan);
	struct ast_sip_session *session = pvt->session;
	struct fixup_data fix_data;
	fix_data.session = session;
	fix_data.chan = newchan;

	if (session->channel != oldchan) {
		return -1;
	}

	if (ast_sip_push_task_synchronous(session->serializer, fixup, &fix_data)) {
		ast_log(LOG_WARNING, "Unable to perform channel fixup\n");
		return -1;
	}

	return 0;
}

struct indicate_data {
	struct ast_sip_session *session;
	int condition;
	int response_code;
	void *frame_data;
	size_t datalen;
};

static void indicate_data_destroy(void *obj)
{
	struct indicate_data *ind_data = obj;
	ast_free(ind_data->frame_data);
	ao2_ref(ind_data->session, -1);
}

static struct indicate_data *indicate_data_alloc(struct ast_sip_session *session,
		int condition, int response_code, const void *frame_data, size_t datalen)
{
	struct indicate_data *ind_data = ao2_alloc(sizeof(*ind_data), indicate_data_destroy);
	if (!ind_data) {
		return NULL;
	}
	ind_data->frame_data = ast_malloc(datalen);
	if (!ind_data->frame_data) {
		ao2_ref(ind_data, -1);
		return NULL;
	}
	memcpy(ind_data->frame_data, frame_data, datalen);
	ind_data->datalen = datalen;
	ind_data->condition = condition;
	ind_data->response_code = response_code;
	ao2_ref(session, +1);
	ind_data->session = session;
	return ind_data;
}

static int indicate(void *data)
{
	struct indicate_data *ind_data = data;
	struct ast_sip_session *session = ind_data->session;
	int response_code = ind_data->response_code;
	pjsip_tx_data *packet = NULL;

	if (pjsip_inv_answer(session->inv_session, response_code, NULL, NULL, &packet) == PJ_SUCCESS) {
		ast_sip_session_send_response(session, packet);
	}

	ao2_ref(ind_data, -1);
	return 0;
}

/*! \brief Send SIP INFO with video update request */
static int transmit_info_with_vidupdate(void *data)
{
	const char * xml =
		"<?xml version=\"1.0\" encoding=\"utf-8\" ?>\r\n"
		" <media_control>\r\n"
		"  <vc_primitive>\r\n"
		"   <to_encoder>\r\n"
		"    <picture_fast_update/>\r\n"
		"   </to_encoder>\r\n"
		"  </vc_primitive>\r\n"
		" </media_control>\r\n";

	const struct ast_sip_body body = {
		.type = "application",
		.subtype = "media_control+xml",
		.body_text = xml
	};

	struct ast_sip_session *session = data;
	struct pjsip_tx_data *tdata;

	if (ast_sip_create_request("INFO", session->inv_session->dlg, session->endpoint, NULL, &tdata)) {
		ast_log(LOG_ERROR, "Could not create text video update INFO request\n");
		return -1;
	}
	if (ast_sip_add_body(tdata, &body)) {
		ast_log(LOG_ERROR, "Could not add body to text video update INFO request\n");
		return -1;
	}
	ast_sip_session_send_request(session, tdata);

	return 0;
}

/*! \brief Function called by core to ask the channel to indicate some sort of condition */
static int gulp_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen)
{
	int res = 0;
	struct gulp_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ast_sip_session *session = pvt->session;
	struct ast_sip_session_media *media;
	int response_code = 0;

	switch (condition) {
	case AST_CONTROL_RINGING:
		if (ast_channel_state(ast) == AST_STATE_RING) {
			response_code = 180;
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_BUSY:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			response_code = 486;
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_CONGESTION:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			response_code = 503;
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_INCOMPLETE:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			response_code = 484;
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_PROCEEDING:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			response_code = 100;
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_PROGRESS:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			response_code = 183;
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_VIDUPDATE:
		media = pvt->media[SIP_MEDIA_VIDEO];
		if (media && media->rtp) {
			ast_sip_push_task(session->serializer, transmit_info_with_vidupdate, session);
		} else
			res = -1;
		break;
	case AST_CONTROL_UPDATE_RTP_PEER:
	case AST_CONTROL_PVT_CAUSE_CODE:
		break;
	case AST_CONTROL_HOLD:
		ast_moh_start(ast, data, NULL);
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(ast);
		break;
	case AST_CONTROL_SRCUPDATE:
		break;
	case AST_CONTROL_SRCCHANGE:
		break;
	case -1:
		res = -1;
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d\n", condition);
		res = -1;
		break;
	}

	if (!res && response_code) {
		struct indicate_data *ind_data = indicate_data_alloc(session, condition, response_code, data, datalen);
		if (ind_data) {
			res = ast_sip_push_task(session->serializer, indicate, ind_data);
			if (res) {
				ast_log(LOG_NOTICE, "Cannot send response code %d to endpoint %s. Could not queue task properly\n",
						response_code, ast_sorcery_object_get_id(session->endpoint));
				ao2_cleanup(ind_data);
			}
		} else {
			res = -1;
		}
	}

	return res;
}

/*! \brief Function called by core to start a DTMF digit */
static int gulp_digit_begin(struct ast_channel *chan, char digit)
{
	struct gulp_pvt *pvt = ast_channel_tech_pvt(chan);
	struct ast_sip_session *session = pvt->session;
	int res = 0;
	struct ast_sip_session_media *media = pvt->media[SIP_MEDIA_AUDIO];

	switch (session->endpoint->dtmf) {
	case AST_SIP_DTMF_RFC_4733:
		if (!media || !media->rtp) {
			return -1;
		}

		ast_rtp_instance_dtmf_begin(media->rtp, digit);
	case AST_SIP_DTMF_NONE:
		break;
	case AST_SIP_DTMF_INBAND:
		res = -1;
		break;
	default:
		break;
	}

	return res;
}

struct info_dtmf_data {
	struct ast_sip_session *session;
	char digit;
	unsigned int duration;
};

static void info_dtmf_data_destroy(void *obj)
{
	struct info_dtmf_data *dtmf_data = obj;
	ao2_ref(dtmf_data->session, -1);
}

static struct info_dtmf_data *info_dtmf_data_alloc(struct ast_sip_session *session, char digit, unsigned int duration)
{
	struct info_dtmf_data *dtmf_data = ao2_alloc(sizeof(*dtmf_data), info_dtmf_data_destroy);
	if (!dtmf_data) {
		return NULL;
	}
	ao2_ref(session, +1);
	dtmf_data->session = session;
	dtmf_data->digit = digit;
	dtmf_data->duration = duration;
	return dtmf_data;
}

static int transmit_info_dtmf(void *data)
{
	RAII_VAR(struct info_dtmf_data *, dtmf_data, data, ao2_cleanup);

	struct ast_sip_session *session = dtmf_data->session;
	struct pjsip_tx_data *tdata;

	RAII_VAR(struct ast_str *, body_text, NULL, ast_free_ptr);

	struct ast_sip_body body = {
		.type = "application",
		.subtype = "dtmf-relay",
	};

	if (!(body_text = ast_str_create(32))) {
		ast_log(LOG_ERROR, "Could not allocate buffer for INFO DTMF.\n");
		return -1;
	}
	ast_str_set(&body_text, 0, "Signal=%c\r\nDuration=%u\r\n", dtmf_data->digit, dtmf_data->duration);

	body.body_text = ast_str_buffer(body_text);

	if (ast_sip_create_request("INFO", session->inv_session->dlg, session->endpoint, NULL, &tdata)) {
		ast_log(LOG_ERROR, "Could not create DTMF INFO request\n");
		return -1;
	}
	if (ast_sip_add_body(tdata, &body)) {
		ast_log(LOG_ERROR, "Could not add body to DTMF INFO request\n");
		pjsip_tx_data_dec_ref(tdata);
		return -1;
	}
	ast_sip_session_send_request(session, tdata);

	return 0;
}

/*! \brief Function called by core to stop a DTMF digit */
static int gulp_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct gulp_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ast_sip_session *session = pvt->session;
	int res = 0;
	struct ast_sip_session_media *media = pvt->media[SIP_MEDIA_AUDIO];

	switch (session->endpoint->dtmf) {
	case AST_SIP_DTMF_INFO:
	{
		struct info_dtmf_data *dtmf_data = info_dtmf_data_alloc(session, digit, duration);

		if (!dtmf_data) {
			return -1;
		}

		if (ast_sip_push_task(session->serializer, transmit_info_dtmf, dtmf_data)) {
			ast_log(LOG_WARNING, "Error sending DTMF via INFO.\n");
			ao2_cleanup(dtmf_data);
			return -1;
		}
		break;
	}
	case AST_SIP_DTMF_RFC_4733:
		if (!media || !media->rtp) {
			return -1;
		}

		ast_rtp_instance_dtmf_end_with_duration(media->rtp, digit, duration);
	case AST_SIP_DTMF_NONE:
		break;
	case AST_SIP_DTMF_INBAND:
		res = -1;
		break;
	}

	return res;
}

static int call(void *data)
{
	struct ast_sip_session *session = data;
	pjsip_tx_data *packet;

	if (pjsip_inv_invite(session->inv_session, &packet) != PJ_SUCCESS) {
		ast_queue_hangup(session->channel);
	} else {
		ast_sip_session_send_request(session, packet);
	}

	ao2_ref(session, -1);
	return 0;
}

/*! \brief Function called by core to actually start calling a remote party */
static int gulp_call(struct ast_channel *ast, const char *dest, int timeout)
{
	struct gulp_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ast_sip_session *session = pvt->session;

	ao2_ref(session, +1);
	if (ast_sip_push_task(session->serializer, call, session)) {
		ast_log(LOG_WARNING, "Error attempting to place outbound call to call '%s'\n", dest);
		ao2_cleanup(session);
		return -1;
	}
	return 0;
}

/*! \brief Internal function which translates from Asterisk cause codes to SIP response codes */
static int hangup_cause2sip(int cause)
{
	switch (cause) {
	case AST_CAUSE_UNALLOCATED:             /* 1 */
	case AST_CAUSE_NO_ROUTE_DESTINATION:    /* 3 IAX2: Can't find extension in context */
	case AST_CAUSE_NO_ROUTE_TRANSIT_NET:    /* 2 */
		return 404;
	case AST_CAUSE_CONGESTION:              /* 34 */
	case AST_CAUSE_SWITCH_CONGESTION:       /* 42 */
		return 503;
	case AST_CAUSE_NO_USER_RESPONSE:        /* 18 */
		return 408;
	case AST_CAUSE_NO_ANSWER:               /* 19 */
	case AST_CAUSE_UNREGISTERED:        /* 20 */
		return 480;
	case AST_CAUSE_CALL_REJECTED:           /* 21 */
		return 403;
	case AST_CAUSE_NUMBER_CHANGED:          /* 22 */
		return 410;
	case AST_CAUSE_NORMAL_UNSPECIFIED:      /* 31 */
		return 480;
	case AST_CAUSE_INVALID_NUMBER_FORMAT:
		return 484;
	case AST_CAUSE_USER_BUSY:
		return 486;
	case AST_CAUSE_FAILURE:
		return 500;
	case AST_CAUSE_FACILITY_REJECTED:       /* 29 */
		return 501;
	case AST_CAUSE_CHAN_NOT_IMPLEMENTED:
		return 503;
	case AST_CAUSE_DESTINATION_OUT_OF_ORDER:
		return 502;
	case AST_CAUSE_BEARERCAPABILITY_NOTAVAIL:       /* Can't find codec to connect to host */
		return 488;
	case AST_CAUSE_INTERWORKING:    /* Unspecified Interworking issues */
		return 500;
	case AST_CAUSE_NOTDEFINED:
	default:
		ast_debug(1, "AST hangup cause %d (no match found in PJSIP)\n", cause);
		return 0;
	}

	/* Never reached */
	return 0;
}

struct hangup_data {
	int cause;
	struct ast_channel *chan;
};

static void hangup_data_destroy(void *obj)
{
	struct hangup_data *h_data = obj;
	h_data->chan = ast_channel_unref(h_data->chan);
}

static struct hangup_data *hangup_data_alloc(int cause, struct ast_channel *chan)
{
	struct hangup_data *h_data = ao2_alloc(sizeof(*h_data), hangup_data_destroy);
	if (!h_data) {
		return NULL;
	}
	h_data->cause = cause;
	h_data->chan = ast_channel_ref(chan);
	return h_data;
}

static int hangup(void *data)
{
	pj_status_t status;
	pjsip_tx_data *packet = NULL;
	struct hangup_data *h_data = data;
	struct ast_channel *ast = h_data->chan;
	struct gulp_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ast_sip_session *session = pvt->session;
	int cause = h_data->cause;

	if (((status = pjsip_inv_end_session(session->inv_session, cause ? cause : 603, NULL, &packet)) == PJ_SUCCESS) && packet) {
		if (packet->msg->type == PJSIP_RESPONSE_MSG) {
			ast_sip_session_send_response(session, packet);
		} else {
			ast_sip_session_send_request(session, packet);
		}
	}

	session->channel = NULL;
	ast_channel_tech_pvt_set(ast, NULL);

	ao2_cleanup(pvt);
	ao2_cleanup(h_data);
	return 0;
}

/*! \brief Function called by core to hang up a Gulp session */
static int gulp_hangup(struct ast_channel *ast)
{
	struct gulp_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ast_sip_session *session = pvt->session;
	int cause = hangup_cause2sip(ast_channel_hangupcause(session->channel));
	struct hangup_data *h_data = hangup_data_alloc(cause, ast);
	if (!h_data) {
		goto failure;
	}

	if (ast_sip_push_task(session->serializer, hangup, h_data)) {
		ast_log(LOG_WARNING, "Unable to push hangup task to the threadpool. Expect bad things\n");
		goto failure;
	}
	return 0;

failure:
	/* Go ahead and do our cleanup of the session and channel even if we're not going
	 * to be able to send our SIP request/response
	 */
	ao2_cleanup(h_data);
	session->channel = NULL;
	ast_channel_tech_pvt_set(ast, NULL);

	ao2_cleanup(pvt);
	return -1;
}

struct request_data {
	struct ast_sip_session *session;
	struct ast_format_cap *caps;
	const char *dest;
	int cause;
};

static int request(void *obj)
{
	struct request_data *req_data = obj;
	char *tmp = ast_strdupa(req_data->dest), *endpoint_name = NULL, *request_user = NULL;
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	struct ast_sip_session *session = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(endpoint);
		AST_APP_ARG(aor);
	);

	if (ast_strlen_zero(tmp)) {
		ast_log(LOG_ERROR, "Unable to create Gulp channel with empty destination\n");
		req_data->cause = AST_CAUSE_CHANNEL_UNACCEPTABLE;
		return -1;
	}

	AST_NONSTANDARD_APP_ARGS(args, tmp, '/');

	/* If a request user has been specified extract it from the endpoint name portion */
	if ((endpoint_name = strchr(args.endpoint, '@'))) {
		request_user = args.endpoint;
		*endpoint_name++ = '\0';
	} else {
		endpoint_name = args.endpoint;
	}

	if (ast_strlen_zero(endpoint_name)) {
		ast_log(LOG_ERROR, "Unable to create Gulp channel with empty endpoint name\n");
		req_data->cause = AST_CAUSE_CHANNEL_UNACCEPTABLE;
	} else if (!(endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", endpoint_name))) {
		ast_log(LOG_ERROR, "Unable to create Gulp channel - endpoint '%s' was not found\n", endpoint_name);
		req_data->cause = AST_CAUSE_NO_ROUTE_DESTINATION;
		return -1;
	}

	if (!(session = ast_sip_session_create_outgoing(endpoint, args.aor, request_user, req_data->caps))) {
		req_data->cause = AST_CAUSE_NO_ROUTE_DESTINATION;
		return -1;
	}

	req_data->session = session;

	return 0;
}

/*! \brief Function called by core to create a new outgoing Gulp session */
static struct ast_channel *gulp_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct request_data req_data;
	struct ast_sip_session *session;

	req_data.caps = cap;
	req_data.dest = data;

	if (ast_sip_push_task_synchronous(NULL, request, &req_data)) {
		*cause = req_data.cause;
		return NULL;
	}

	session = req_data.session;

	if (!(session->channel = gulp_new(session, AST_STATE_DOWN, NULL, NULL, requestor ? ast_channel_linkedid(requestor) : NULL, NULL))) {
		/* Session needs to be terminated prematurely */
		return NULL;
	}

	return session->channel;
}

/*! \brief Function called by core to send text on Gulp session */
static int gulp_sendtext(struct ast_channel *ast, const char *text)
{
	return 0;
}

/*! \brief Convert SIP hangup causes to Asterisk hangup causes */
static int hangup_sip2cause(int cause)
{
	/* Possible values taken from causes.h */

	switch(cause) {
	case 401:       /* Unauthorized */
		return AST_CAUSE_CALL_REJECTED;
	case 403:       /* Not found */
		return AST_CAUSE_CALL_REJECTED;
	case 404:       /* Not found */
		return AST_CAUSE_UNALLOCATED;
	case 405:       /* Method not allowed */
		return AST_CAUSE_INTERWORKING;
	case 407:       /* Proxy authentication required */
		return AST_CAUSE_CALL_REJECTED;
	case 408:       /* No reaction */
		return AST_CAUSE_NO_USER_RESPONSE;
	case 409:       /* Conflict */
		return AST_CAUSE_NORMAL_TEMPORARY_FAILURE;
	case 410:       /* Gone */
		return AST_CAUSE_NUMBER_CHANGED;
	case 411:       /* Length required */
		return AST_CAUSE_INTERWORKING;
	case 413:       /* Request entity too large */
		return AST_CAUSE_INTERWORKING;
	case 414:       /* Request URI too large */
		return AST_CAUSE_INTERWORKING;
	case 415:       /* Unsupported media type */
		return AST_CAUSE_INTERWORKING;
	case 420:       /* Bad extension */
		return AST_CAUSE_NO_ROUTE_DESTINATION;
	case 480:       /* No answer */
		return AST_CAUSE_NO_ANSWER;
	case 481:       /* No answer */
		return AST_CAUSE_INTERWORKING;
	case 482:       /* Loop detected */
		return AST_CAUSE_INTERWORKING;
	case 483:       /* Too many hops */
		return AST_CAUSE_NO_ANSWER;
	case 484:       /* Address incomplete */
		return AST_CAUSE_INVALID_NUMBER_FORMAT;
	case 485:       /* Ambiguous */
		return AST_CAUSE_UNALLOCATED;
	case 486:       /* Busy everywhere */
		return AST_CAUSE_BUSY;
	case 487:       /* Request terminated */
		return AST_CAUSE_INTERWORKING;
	case 488:       /* No codecs approved */
		return AST_CAUSE_BEARERCAPABILITY_NOTAVAIL;
	case 491:       /* Request pending */
		return AST_CAUSE_INTERWORKING;
	case 493:       /* Undecipherable */
		return AST_CAUSE_INTERWORKING;
	case 500:       /* Server internal failure */
		return AST_CAUSE_FAILURE;
	case 501:       /* Call rejected */
		return AST_CAUSE_FACILITY_REJECTED;
	case 502:
		return AST_CAUSE_DESTINATION_OUT_OF_ORDER;
	case 503:       /* Service unavailable */
		return AST_CAUSE_CONGESTION;
	case 504:       /* Gateway timeout */
		return AST_CAUSE_RECOVERY_ON_TIMER_EXPIRE;
	case 505:       /* SIP version not supported */
		return AST_CAUSE_INTERWORKING;
	case 600:       /* Busy everywhere */
		return AST_CAUSE_USER_BUSY;
	case 603:       /* Decline */
		return AST_CAUSE_CALL_REJECTED;
	case 604:       /* Does not exist anywhere */
		return AST_CAUSE_UNALLOCATED;
	case 606:       /* Not acceptable */
		return AST_CAUSE_BEARERCAPABILITY_NOTAVAIL;
	default:
		if (cause < 500 && cause >= 400) {
			/* 4xx class error that is unknown - someting wrong with our request */
			return AST_CAUSE_INTERWORKING;
		} else if (cause < 600 && cause >= 500) {
			/* 5xx class error - problem in the remote end */
			return AST_CAUSE_CONGESTION;
		} else if (cause < 700 && cause >= 600) {
			/* 6xx - global errors in the 4xx class */
			return AST_CAUSE_INTERWORKING;
		}
		return AST_CAUSE_NORMAL;
	}
	/* Never reached */
	return 0;
}

static void gulp_session_begin(struct ast_sip_session *session)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);

	if (session->endpoint->direct_media_glare_mitigation ==
			AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_NONE) {
		return;
	}

	datastore = ast_sip_session_alloc_datastore(&direct_media_mitigation_info,
			"direct_media_glare_mitigation");

	if (!datastore) {
		return;
	}

	ast_sip_session_add_datastore(session, datastore);
}

/*! \brief Function called when the session ends */
static void gulp_session_end(struct ast_sip_session *session)
{
	if (!session->channel) {
		return;
	}

	if (!ast_channel_hangupcause(session->channel) && session->inv_session) {
		int cause = hangup_sip2cause(session->inv_session->cause);

		ast_queue_hangup_with_cause(session->channel, cause);
	} else {
		ast_queue_hangup(session->channel);
	}
}

/*! \brief Function called when a request is received on the session */
static int gulp_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	pjsip_tx_data *packet = NULL;
	int res = AST_PBX_FAILED;

	if (session->channel) {
		return 0;
	}

	if (!(session->channel = gulp_new(session, AST_STATE_DOWN, session->exten, NULL, NULL, NULL))) {
		if (pjsip_inv_end_session(session->inv_session, 503, NULL, &packet) == PJ_SUCCESS) {
			ast_sip_session_send_response(session, packet);
		}

		ast_log(LOG_ERROR, "Failed to allocate new GULP channel on incoming SIP INVITE\n");
		return -1;
	}

	ast_setstate(session->channel, AST_STATE_RING);
	res = ast_pbx_start(session->channel);

	switch (res) {
	case AST_PBX_FAILED:
		ast_log(LOG_WARNING, "Failed to start PBX ;(\n");
		ast_channel_hangupcause_set(session->channel, AST_CAUSE_SWITCH_CONGESTION);
		ast_hangup(session->channel);
		break;
	case AST_PBX_CALL_LIMIT:
		ast_log(LOG_WARNING, "Failed to start PBX (call limit reached) \n");
		ast_channel_hangupcause_set(session->channel, AST_CAUSE_SWITCH_CONGESTION);
		ast_hangup(session->channel);
		break;
	case AST_PBX_SUCCESS:
	default:
		break;
	}

	ast_debug(3, "Started PBX on new GULP channel %s\n", ast_channel_name(session->channel));

	return (res == AST_PBX_SUCCESS) ? 0 : -1;
}

/*! \brief Function called when a response is received on the session */
static void gulp_incoming_response(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	struct pjsip_status_line status = rdata->msg_info.msg->line.status;

	if (!session->channel) {
		return;
	}

	switch (status.code) {
	case 180:
		ast_queue_control(session->channel, AST_CONTROL_RINGING);
		if (ast_channel_state(session->channel) != AST_STATE_UP) {
			ast_setstate(session->channel, AST_STATE_RINGING);
		}
		break;
	case 183:
		ast_queue_control(session->channel, AST_CONTROL_PROGRESS);
		break;
	case 200:
		ast_queue_control(session->channel, AST_CONTROL_ANSWER);
		break;
	default:
		break;
	}
}

static int gulp_incoming_ack(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	if (rdata->msg_info.msg->line.req.method.id == PJSIP_ACK_METHOD) {
		if (session->endpoint->direct_media) {
			ast_queue_control(session->channel, AST_CONTROL_SRCCHANGE);
		}
	}
	return 0;
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the 
 * configuration file or other non-critical problem return 
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	if (!(gulp_tech.capabilities = ast_format_cap_alloc())) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_format_cap_add_all_by_type(gulp_tech.capabilities, AST_FORMAT_TYPE_AUDIO);

	ast_rtp_glue_register(&gulp_rtp_glue);

	if (ast_channel_register(&gulp_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", channel_type);
		goto end;
	}

	if (ast_custom_function_register(&gulp_dial_contacts_function)) {
		ast_log(LOG_ERROR, "Unable to register GULP_DIAL_CONTACTS dialplan function\n");
		goto end;
	}

	if (ast_sip_session_register_supplement(&gulp_supplement)) {
		ast_log(LOG_ERROR, "Unable to register Gulp supplement\n");
		goto end;
	}

	if (ast_sip_session_register_supplement(&gulp_ack_supplement)) {
		ast_log(LOG_ERROR, "Unable to register Gulp ACK supplement\n");
		ast_sip_session_unregister_supplement(&gulp_supplement);
		goto end;
	}

	return 0;

end:
	ast_custom_function_unregister(&gulp_dial_contacts_function);
	ast_channel_unregister(&gulp_tech);
	ast_rtp_glue_unregister(&gulp_rtp_glue);

	return AST_MODULE_LOAD_FAILURE;
}

/*! \brief Reload module */
static int reload(void)
{
	return -1;
}

/*! \brief Unload the Gulp channel from Asterisk */
static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&gulp_supplement);
	ast_custom_function_unregister(&gulp_dial_contacts_function);
	ast_channel_unregister(&gulp_tech);
	ast_rtp_glue_unregister(&gulp_rtp_glue);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Gulp SIP Channel Driver",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_CHANNEL_DRIVER,
	       );
