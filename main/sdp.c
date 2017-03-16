/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
 *
 * George Joseph <gjoseph@digium.com>
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
#include "asterisk/utils.h"
#include "asterisk/netsock2.h"
#include "asterisk/codec.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/sdp_state.h"
#include "asterisk/sdp_options.h"
#include "asterisk/sdp_translator.h"
#include "asterisk/sdp.h"
#include "asterisk/vector.h"
#include "asterisk/utils.h"
#include "asterisk/stream.h"
#include "sdp_private.h"

void ast_sdp_a_free(struct ast_sdp_a_line *a_line)
{
	ast_free(a_line);
}

void ast_sdp_a_lines_free(struct ast_sdp_a_lines *a_lines)
{
	if (!a_lines) {
		return;
	}

	AST_VECTOR_CALLBACK_VOID(a_lines, ast_sdp_a_free);
	AST_VECTOR_FREE(a_lines);
	ast_free(a_lines);
}

void ast_sdp_c_free(struct ast_sdp_c_line *c_line)
{
	ast_free(c_line);
}

void ast_sdp_payload_free(struct ast_sdp_payload *payload)
{
	ast_free(payload);
}

void ast_sdp_payloads_free(struct ast_sdp_payloads *payloads)
{
	if (!payloads) {
		return;
	}

	AST_VECTOR_CALLBACK_VOID(payloads, ast_sdp_payload_free);
	AST_VECTOR_FREE(payloads);
	ast_free(payloads);
}

void ast_sdp_m_free(struct ast_sdp_m_line *m_line)
{
	if (!m_line) {
		return;
	}

	ast_sdp_a_lines_free(m_line->a_lines);
	ast_sdp_payloads_free(m_line->payloads);
	ast_sdp_c_free(m_line->c_line);
	ast_free(m_line);
}

void ast_sdp_m_lines_free(struct ast_sdp_m_lines *m_lines)
{
	if (!m_lines) {
		return;
	}

	AST_VECTOR_CALLBACK_VOID(m_lines, ast_sdp_m_free);
	AST_VECTOR_FREE(m_lines);
	ast_free(m_lines);
}

void ast_sdp_o_free(struct ast_sdp_o_line *o_line)
{
	ast_free(o_line);
}

void ast_sdp_s_free(struct ast_sdp_s_line *s_line)
{
	ast_free(s_line);
}

void ast_sdp_t_free(struct ast_sdp_t_line *t_line)
{
	ast_free(t_line);
}

void ast_sdp_free(struct ast_sdp *sdp)
{
	if (!sdp) {
		return;
	}

	ast_sdp_o_free(sdp->o_line);
	ast_sdp_s_free(sdp->s_line);
	ast_sdp_c_free(sdp->c_line);
	ast_sdp_t_free(sdp->t_line);
	ast_sdp_a_lines_free(sdp->a_lines);
	ast_sdp_m_lines_free(sdp->m_lines);
	ast_free(sdp);
}

#define COPY_STR_AND_ADVANCE(p, dest, source) \
({ \
	dest = p; \
	strcpy(dest, source); \
	p += (strlen(source) + 1); \
})

struct ast_sdp_a_line *ast_sdp_a_alloc(const char *name, const char *value)
{
	struct ast_sdp_a_line *a_line;
	size_t len;
	char *p;

	ast_assert(!ast_strlen_zero(name));

	if (ast_strlen_zero(value)) {
		value = "";
	}

	len = sizeof(*a_line) + strlen(name) + strlen(value) + 2;
	a_line = ast_calloc(1, len);
	if (!a_line) {
		return NULL;
	}

	p = ((char *)a_line) + sizeof(*a_line);

	COPY_STR_AND_ADVANCE(p, a_line->name, name);
	COPY_STR_AND_ADVANCE(p, a_line->value, value);

	return a_line;
}

struct ast_sdp_c_line *ast_sdp_c_alloc(const char *address_type, const char *address)
{
	struct ast_sdp_c_line *c_line;
	size_t len;
	char *p;

	ast_assert(!ast_strlen_zero(address_type) && !ast_strlen_zero(address));

	len = sizeof(*c_line) + strlen(address_type) + strlen(address) + 2;
	c_line = ast_calloc(1, len);
	if (!c_line) {
		return NULL;
	}

	p = ((char *)c_line) + sizeof(*c_line);

	COPY_STR_AND_ADVANCE(p, c_line->address_type, address_type);
	COPY_STR_AND_ADVANCE(p, c_line->address, address);

	return c_line;
}

struct ast_sdp_payload *ast_sdp_payload_alloc(const char *fmt)
{
	struct ast_sdp_payload *payload;
	size_t len;

	ast_assert(!ast_strlen_zero(fmt));

	len = sizeof(*payload) + strlen(fmt) + 1;
	payload = ast_calloc(1, len);
	if (!payload) {
		return NULL;
	}

	payload->fmt = ((char *)payload) + sizeof(*payload);
	strcpy(payload->fmt, fmt);  /* Safe */

	return payload;
}

struct ast_sdp_m_line *ast_sdp_m_alloc(const char *type, uint16_t port,
	uint16_t port_count, const char *proto, struct ast_sdp_c_line *c_line)
{
	struct ast_sdp_m_line *m_line;
	size_t len;
	char *p;

	ast_assert(!ast_strlen_zero(type) && !ast_strlen_zero(proto));

	len = sizeof(*m_line) + strlen(type) + strlen(proto) + 2;
	m_line = ast_calloc(1, len);
	if (!m_line) {
		return NULL;
	}

	m_line->a_lines = ast_calloc(1, sizeof(*m_line->a_lines));
	if (!m_line->a_lines) {
		ast_sdp_m_free(m_line);
		return NULL;
	}
	if (AST_VECTOR_INIT(m_line->a_lines, 20)) {
		ast_sdp_m_free(m_line);
		return NULL;
	}

	m_line->payloads = ast_calloc(1, sizeof(*m_line->payloads));
	if (!m_line->payloads) {
		ast_sdp_m_free(m_line);
		return NULL;
	}
	if (AST_VECTOR_INIT(m_line->payloads, 20)) {
		ast_sdp_m_free(m_line);
		return NULL;
	}

	p = ((char *)m_line) + sizeof(*m_line);

	COPY_STR_AND_ADVANCE(p, m_line->type, type);
	COPY_STR_AND_ADVANCE(p, m_line->proto, proto);
	m_line->port = port;
	m_line->port_count = port_count;
	m_line->c_line = c_line;

	return m_line;
}

struct ast_sdp_s_line *ast_sdp_s_alloc(const char *session_name)
{
	struct ast_sdp_s_line *s_line;
	size_t len;

	if (ast_strlen_zero(session_name)) {
		session_name = " ";
	}

	len = sizeof(*s_line) + strlen(session_name) + 1;
	s_line = ast_calloc(1, len);
	if (!s_line) {
		return NULL;
	}

	s_line->session_name = ((char *)s_line) + sizeof(*s_line);
	strcpy(s_line->session_name, session_name);  /* Safe */

	return s_line;
}

struct ast_sdp_t_line *ast_sdp_t_alloc(uint64_t start_time, uint64_t stop_time)
{
	struct ast_sdp_t_line *t_line;

	t_line = ast_calloc(1, sizeof(*t_line));
	if (!t_line) {
		return NULL;
	}

	t_line->start_time = start_time;
	t_line->stop_time = stop_time;

	return t_line;
}

struct ast_sdp_o_line *ast_sdp_o_alloc(const char *username, uint64_t session_id,
	uint64_t session_version, const char *address_type, const char *address)
{
	struct ast_sdp_o_line *o_line;
	size_t len;
	char *p;

	ast_assert(!ast_strlen_zero(username) && !ast_strlen_zero(address_type)
		&& !ast_strlen_zero(address));

	len = sizeof(*o_line) + strlen(username) + strlen(address_type) + strlen(address) + 3;
	o_line = ast_calloc(1, len);
	if (!o_line) {
		return NULL;
	}

	o_line->session_id = session_id;
	o_line->session_version = session_version;

	p = ((char *)o_line) + sizeof(*o_line);

	COPY_STR_AND_ADVANCE(p, o_line->username, username);
	COPY_STR_AND_ADVANCE(p, o_line->address_type, address_type);
	COPY_STR_AND_ADVANCE(p, o_line->address, address);

	return o_line;
}

struct ast_sdp *ast_sdp_alloc(struct ast_sdp_o_line *o_line,
	struct ast_sdp_c_line *c_line, struct ast_sdp_s_line *s_line,
	struct ast_sdp_t_line *t_line)
{
	struct ast_sdp *new_sdp;

	new_sdp = ast_calloc(1, sizeof *new_sdp);
	if (!new_sdp) {
		return NULL;
	}

	new_sdp->a_lines = ast_calloc(1, sizeof(*new_sdp->a_lines));
	if (!new_sdp->a_lines) {
		ast_sdp_free(new_sdp);
		return NULL;
	}
	if (AST_VECTOR_INIT(new_sdp->a_lines, 20)) {
		ast_sdp_free(new_sdp);
		return NULL;
	}

	new_sdp->m_lines = ast_calloc(1, sizeof(*new_sdp->m_lines));
	if (!new_sdp->m_lines) {
		ast_sdp_free(new_sdp);
		return NULL;
	}
	if (AST_VECTOR_INIT(new_sdp->m_lines, 20)) {
		ast_sdp_free(new_sdp);
		return NULL;
	}

	new_sdp->o_line = o_line;
	new_sdp->c_line = c_line;
	new_sdp->s_line = s_line;
	new_sdp->t_line = t_line;

	return new_sdp;
}

int ast_sdp_add_a(struct ast_sdp *sdp, struct ast_sdp_a_line *a_line)
{
	ast_assert(sdp && a_line);

	return AST_VECTOR_APPEND(sdp->a_lines, a_line);
}

int ast_sdp_get_a_count(const struct ast_sdp *sdp)
{
	ast_assert(sdp != NULL);

	return AST_VECTOR_SIZE(sdp->a_lines);
}

struct ast_sdp_a_line *ast_sdp_get_a(const struct ast_sdp *sdp, int index)
{
	ast_assert(sdp != NULL);

	return AST_VECTOR_GET(sdp->a_lines, index);
}

int ast_sdp_add_m(struct ast_sdp *sdp, struct ast_sdp_m_line *m_line)
{
	ast_assert(sdp && m_line);

	return AST_VECTOR_APPEND(sdp->m_lines, m_line);
}

int ast_sdp_get_m_count(const struct ast_sdp *sdp)
{
	ast_assert(sdp != NULL);

	return AST_VECTOR_SIZE(sdp->m_lines);
}

struct ast_sdp_m_line *ast_sdp_get_m(const struct ast_sdp *sdp, int index)
{
	ast_assert(sdp != NULL);

	return AST_VECTOR_GET(sdp->m_lines, index);
}

int ast_sdp_m_add_a(struct ast_sdp_m_line *m_line, struct ast_sdp_a_line *a_line)
{
	ast_assert(m_line && a_line);

	return AST_VECTOR_APPEND(m_line->a_lines, a_line);
}

int ast_sdp_m_get_a_count(const struct ast_sdp_m_line *m_line)
{
	ast_assert(m_line != NULL);

	return AST_VECTOR_SIZE(m_line->a_lines);
}

struct ast_sdp_a_line *ast_sdp_m_get_a(const struct ast_sdp_m_line *m_line, int index)
{
	ast_assert(m_line != NULL);

	return AST_VECTOR_GET(m_line->a_lines, index);
}

int ast_sdp_m_add_payload(struct ast_sdp_m_line *m_line, struct ast_sdp_payload *payload)
{
	ast_assert(m_line && payload);

	return AST_VECTOR_APPEND(m_line->payloads, payload);
}

int ast_sdp_m_get_payload_count(const struct ast_sdp_m_line *m_line)
{
	ast_assert(m_line != NULL);

	return AST_VECTOR_SIZE(m_line->payloads);
}

struct ast_sdp_payload *ast_sdp_m_get_payload(const struct ast_sdp_m_line *m_line, int index)
{
	ast_assert(m_line != NULL);

	return AST_VECTOR_GET(m_line->payloads, index);
}

static int sdp_m_add_fmtp(struct ast_sdp_m_line *m_line, const struct ast_format *format,
	int rtp_code)
{
	struct ast_str *fmtp0 = ast_str_alloca(256);
	char *tmp;

	ast_format_generate_sdp_fmtp(format, rtp_code, &fmtp0);
	if (ast_str_strlen(fmtp0) == 0) {
		return -1;
	}

	tmp = ast_str_buffer(fmtp0) + ast_str_strlen(fmtp0) - 1;
		/* remove any carriage return line feeds */
	while (*tmp == '\r' || *tmp == '\n') --tmp;
	*++tmp = '\0';

	/* ast...generate gives us everything, just need value */
	tmp = strchr(ast_str_buffer(fmtp0), ':');
	if (tmp && tmp[1] != '\0') {
		tmp++;
	} else {
		tmp = ast_str_buffer(fmtp0);
	}

	ast_sdp_m_add_a(m_line, ast_sdp_a_alloc("fmtp", tmp));

	return 0;
}

static int sdp_m_add_rtpmap(struct ast_sdp_m_line *m_line,
	const struct ast_sdp_options *options, int rtp_code, int asterisk_format,
	const struct ast_format *format, int code)
{
	char tmp[64];
	const char *enc_name;
	struct ast_sdp_payload *payload;
	struct ast_sdp_a_line *a_line;

	snprintf(tmp, sizeof(tmp), "%d", rtp_code);
	payload = ast_sdp_payload_alloc(tmp);
	if (!payload || ast_sdp_m_add_payload(m_line, payload)) {
		ast_sdp_payload_free(payload);
		return -1;
	}

	enc_name = ast_rtp_lookup_mime_subtype2(asterisk_format, format, code,
		options->g726_non_standard ? AST_RTP_OPT_G726_NONSTANDARD : 0);

	snprintf(tmp, sizeof(tmp), "%d %s/%d%s%s", rtp_code, enc_name,
		ast_rtp_lookup_sample_rate2(asterisk_format, format, code),
		strcmp(enc_name, "opus") ? "" : "/", strcmp(enc_name, "opus") ? "" : "2");

	a_line = ast_sdp_a_alloc("rtpmap", tmp);
	if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
		ast_sdp_a_free(a_line);
		return -1;
	}

	return 0;
}

int ast_sdp_m_add_format(struct ast_sdp_m_line *m_line, const struct ast_sdp_options *options,
	int rtp_code, int asterisk_format, const struct ast_format *format, int code)
{
	sdp_m_add_rtpmap(m_line, options, rtp_code, asterisk_format, format, code);
	sdp_m_add_fmtp(m_line, format, rtp_code);

	return 0;
}

/* TODO
 * This isn't set anywhere yet.
 */
/*! \brief Scheduler for RTCP purposes */
static struct ast_sched_context *sched;

/*! \brief Internal function which creates an RTP instance */
static struct ast_rtp_instance *create_rtp(const struct ast_sdp_options *options,
	enum ast_media_type media_type)
{
	struct ast_rtp_instance *rtp;
	struct ast_rtp_engine_ice *ice;
	struct ast_sockaddr temp_media_address;
	static struct ast_sockaddr address_rtp;
	struct ast_sockaddr *media_address =  &address_rtp;

	if (options->bind_rtp_to_media_address && !ast_strlen_zero(options->media_address)) {
		ast_sockaddr_parse(&temp_media_address, options->media_address, 0);
		media_address = &temp_media_address;
	} else {
		if (ast_check_ipv6()) {
			ast_sockaddr_parse(&address_rtp, "::", 0);
		} else {
			ast_sockaddr_parse(&address_rtp, "0.0.0.0", 0);
		}
	}

	if (!(rtp = ast_rtp_instance_new(options->rtp_engine, sched, media_address, NULL))) {
		ast_log(LOG_ERROR, "Unable to create RTP instance using RTP engine '%s'\n",
			options->rtp_engine);
		return NULL;
	}

	ast_rtp_instance_set_prop(rtp, AST_RTP_PROPERTY_RTCP, 1);
	ast_rtp_instance_set_prop(rtp, AST_RTP_PROPERTY_NAT, options->rtp_symmetric);

	if (options->ice == AST_SDP_ICE_DISABLED && (ice = ast_rtp_instance_get_ice(rtp))) {
		ice->stop(rtp);
	}

	if (options->telephone_event) {
		ast_rtp_instance_dtmf_mode_set(rtp, AST_RTP_DTMF_MODE_RFC2833);
		ast_rtp_instance_set_prop(rtp, AST_RTP_PROPERTY_DTMF, 1);
	}

	if (media_type == AST_MEDIA_TYPE_AUDIO &&
			(options->tos_audio || options->cos_audio)) {
		ast_rtp_instance_set_qos(rtp, options->tos_audio,
			options->cos_audio, "SIP RTP Audio");
	} else if (media_type == AST_MEDIA_TYPE_VIDEO &&
			(options->tos_video || options->cos_video)) {
		ast_rtp_instance_set_qos(rtp, options->tos_video,
			options->cos_video, "SIP RTP Video");
	}

	ast_rtp_instance_set_last_rx(rtp, time(NULL));

	return rtp;
}

int ast_sdp_add_m_from_stream(struct ast_sdp *sdp, const struct ast_sdp_options *options,
	struct ast_rtp_instance *rtp, const struct ast_stream *stream)
{
	struct ast_sdp_m_line *m_line;
	struct ast_format_cap *caps;
	int i;
	int rtp_code;
	int min_packet_size = 0;
	int max_packet_size = 0;
	enum ast_media_type media_type;
	char tmp[64];
	struct ast_sockaddr address_rtp;
	struct ast_sdp_a_line *a_line;


	ast_assert(sdp && options && rtp && stream);

	media_type = ast_stream_get_type(stream);
	ast_rtp_instance_get_local_address(rtp, &address_rtp);

	m_line = ast_sdp_m_alloc(
		ast_codec_media_type2str(ast_stream_get_type(stream)),
		ast_sockaddr_port(&address_rtp), 1,
		options->encryption != AST_SDP_ENCRYPTION_DISABLED ? "RTP/SAVP" : "RTP/AVP",
		NULL);
	if (!m_line) {
		return -1;
	}

	caps = ast_stream_get_formats(stream);

	for (i = 0; i < ast_format_cap_count(caps); i++) {
		struct ast_format *format = ast_format_cap_get_format(caps, i);

		if ((rtp_code = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(rtp), 1, format, 0)) == -1) {
			ast_log(LOG_WARNING,"Unable to get rtp codec payload code for %s\n", ast_format_get_name(format));
			ao2_ref(format, -1);
			continue;
		}

		if (ast_sdp_m_add_format(m_line, options, rtp_code, 0, format, 0)) {
			ast_sdp_m_free(m_line);
			ao2_ref(format, -1);
			return -1;
		}

		if (ast_format_get_maximum_ms(format) &&
			((ast_format_get_maximum_ms(format) < max_packet_size) || !max_packet_size)) {
			max_packet_size = ast_format_get_maximum_ms(format);
		}

		ao2_ref(format, -1);
	}

	if (media_type != AST_MEDIA_TYPE_VIDEO) {
		for (i = 1LL; i <= AST_RTP_MAX; i <<= 1) {
			if (!(options->telephone_event & i)) {
				continue;
			}

			rtp_code = ast_rtp_codecs_payload_code(
				ast_rtp_instance_get_codecs(rtp), 0, NULL, i);

			if (rtp_code == -1) {
				continue;
			}

			if (sdp_m_add_rtpmap(m_line, options, rtp_code, 0, NULL, i)) {
				continue;
			}

			if (i == AST_RTP_DTMF) {
				snprintf(tmp, sizeof(tmp), "%d 0-16", rtp_code);
				a_line = ast_sdp_a_alloc("fmtp", tmp);
				if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
					ast_sdp_a_free(a_line);
					ast_sdp_m_free(m_line);
					return -1;
				}
			}
		}
	}

	if (ast_sdp_m_get_a_count(m_line) == 0) {
		return 0;
	}

	/* If ptime is set add it as an attribute */
	min_packet_size = ast_rtp_codecs_get_framing(ast_rtp_instance_get_codecs(rtp));
	if (!min_packet_size) {
		min_packet_size = ast_format_cap_get_framing(caps);
	}
	if (min_packet_size) {
		snprintf(tmp, sizeof(tmp), "%d", min_packet_size);

		a_line = ast_sdp_a_alloc("ptime", tmp);
		if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
			ast_sdp_a_free(a_line);
			ast_sdp_m_free(m_line);
			return -1;
		}
	}

	if (max_packet_size) {
		snprintf(tmp, sizeof(tmp), "%d", max_packet_size);
		a_line = ast_sdp_a_alloc("maxptime", tmp);
		if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
			ast_sdp_a_free(a_line);
			ast_sdp_m_free(m_line);
			return -1;
		}
	}

	a_line = ast_sdp_a_alloc(options->locally_held ? "sendonly" : "sendrecv", "");
	if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
		ast_sdp_a_free(a_line);
		ast_sdp_m_free(m_line);
		return -1;
	}

	if (ast_sdp_add_m(sdp, m_line)) {
		ast_sdp_m_free(m_line);
		return -1;
	}

	return 0;
}

struct ast_sdp *ast_sdp_create_from_state(const struct ast_sdp_state *sdp_state)
{
	const struct ast_sdp_options *options;
	RAII_VAR(struct ast_sdp *, sdp, NULL, ao2_cleanup);
	const const struct ast_stream_topology *topology;
	int stream_count;
	int stream_num;
	struct ast_sdp_o_line *o_line = NULL;
	struct ast_sdp_c_line *c_line = NULL;
	struct ast_sdp_s_line *s_line = NULL;
	struct ast_sdp_t_line *t_line = NULL;
	struct ast_rtp_instance *rtp = NULL;
	char *address_type;
	struct timeval tv = ast_tvnow();
	uint32_t t;
	ast_assert(!!sdp_state);

	options = ast_sdp_state_get_options(sdp_state);
	topology = ast_sdp_state_get_local_topology(sdp_state);
	stream_count = ast_stream_topology_get_count(topology);

	t = tv.tv_sec + 2208988800UL;
	address_type = (strchr(options->media_address, ':') ? "IP6" : "IP4");

	o_line = ast_sdp_o_alloc(options->sdpowner, t, t, address_type, options->media_address);
	if (!o_line) {
		goto error;
	}
	c_line = ast_sdp_c_alloc(address_type, options->media_address);
	if (!c_line) {
		goto error;
	}

	s_line = ast_sdp_s_alloc(options->sdpsession);
	if (!s_line) {
		goto error;
	}

	sdp = ast_sdp_alloc(o_line, c_line, s_line, NULL);
	if (!sdp) {
		goto error;
	}

	for (stream_num = 0; stream_num < stream_count; stream_num++) {
		struct ast_stream *stream = ast_stream_topology_get_stream(topology, stream_num);

		rtp = create_rtp(options, ast_stream_get_type(stream));
		if (!rtp) {
			goto error;
		}

		ast_stream_set_data(stream, AST_STREAM_DATA_RTP_INSTANCE,
			rtp, (ast_stream_data_free_fn)&ast_rtp_instance_destroy);

		if (ast_sdp_add_m_from_stream(sdp, options, rtp, stream)) {
			goto error;
		}
	}

	return sdp;

error:
	ao2_cleanup(rtp);
	if (sdp) {
		ast_sdp_free(sdp);
	} else {
		ast_sdp_t_free(t_line);
		ast_sdp_s_free(s_line);
		ast_sdp_c_free(c_line);
		ast_sdp_o_free(o_line);
	}

	return NULL;
}

