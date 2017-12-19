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
#include "asterisk/format_cache.h"
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
	struct ast_sdp_a_line *a_line;
	char *tmp;

	ast_format_generate_sdp_fmtp(format, rtp_code, &fmtp0);
	if (ast_str_strlen(fmtp0) == 0) {
		/* Format doesn't have fmtp attributes */
		return 0;
	}

	tmp = ast_str_buffer(fmtp0) + ast_str_strlen(fmtp0) - 1;
	/* remove any carriage return line feeds */
	while (*tmp == '\r' || *tmp == '\n') --tmp;
	*++tmp = '\0';

	/*
	 * ast...generate gives us everything, just need value
	 *
	 * It can also give multiple fmtp attribute lines. (silk does)
	 */
	tmp = strchr(ast_str_buffer(fmtp0), ':');
	if (tmp && tmp[1] != '\0') {
		tmp++;
	} else {
		tmp = ast_str_buffer(fmtp0);
	}

	a_line = ast_sdp_a_alloc("fmtp", tmp);
	if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
		return -1;
	}

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
	return sdp_m_add_rtpmap(m_line, options, rtp_code, asterisk_format, format, code)
		|| sdp_m_add_fmtp(m_line, format, rtp_code) ? -1 : 0;
}

static int sdp_find_a_common(const struct ast_sdp_a_lines *a_lines, int start,
	const char *attr_name, int payload)
{
	struct ast_sdp_a_line *a_line;
	int idx;

	ast_assert(-1 <= start);

	for (idx = start + 1; idx < AST_VECTOR_SIZE(a_lines); ++idx) {
		int a_line_payload;

		a_line = AST_VECTOR_GET(a_lines, idx);
		if (strcmp(a_line->name, attr_name)) {
			continue;
		}

		if (payload >= 0) {
			int sscanf_res;

			sscanf_res = sscanf(a_line->value, "%30d", &a_line_payload);
			if (sscanf_res == 1 && payload == a_line_payload) {
				return idx;
			}
		} else {
			return idx;
		}
	}

	return -1;
}

int ast_sdp_find_a_first(const struct ast_sdp *sdp, const char *attr_name, int payload)
{
	return sdp_find_a_common(sdp->a_lines, -1, attr_name, payload);
}

int ast_sdp_find_a_next(const struct ast_sdp *sdp, int last, const char *attr_name, int payload)
{
	return sdp_find_a_common(sdp->a_lines, last, attr_name, payload);
}

struct ast_sdp_a_line *ast_sdp_find_attribute(const struct ast_sdp *sdp,
	const char *attr_name, int payload)
{
	int idx;

	idx = ast_sdp_find_a_first(sdp, attr_name, payload);
	if (idx < 0) {
		return NULL;
	}
	return ast_sdp_get_a(sdp, idx);
}

int ast_sdp_m_find_a_first(const struct ast_sdp_m_line *m_line, const char *attr_name,
	int payload)
{
	return sdp_find_a_common(m_line->a_lines, -1, attr_name, payload);
}

int ast_sdp_m_find_a_next(const struct ast_sdp_m_line *m_line, int last,
	const char *attr_name, int payload)
{
	return sdp_find_a_common(m_line->a_lines, last, attr_name, payload);
}

struct ast_sdp_a_line *ast_sdp_m_find_attribute(const struct ast_sdp_m_line *m_line,
	const char *attr_name, int payload)
{
	int idx;

	idx = ast_sdp_m_find_a_first(m_line, attr_name, payload);
	if (idx < 0) {
		return NULL;
	}
	return ast_sdp_m_get_a(m_line, idx);
}

struct ast_sdp_rtpmap *ast_sdp_rtpmap_alloc(int payload, const char *encoding_name,
	int clock_rate, const char *encoding_parameters)
{
	struct ast_sdp_rtpmap *rtpmap;
	char *buf_pos;

	rtpmap = ast_calloc(1, sizeof(*rtpmap) + strlen(encoding_name) + strlen(encoding_parameters) + 2);
	if (!rtpmap) {
		return NULL;
	}

	rtpmap->payload = payload;
	rtpmap->clock_rate = clock_rate;

	buf_pos = rtpmap->buf;
	COPY_STR_AND_ADVANCE(buf_pos, rtpmap->encoding_name, encoding_name);
	COPY_STR_AND_ADVANCE(buf_pos, rtpmap->encoding_parameters, encoding_parameters);

	return rtpmap;
}

void ast_sdp_rtpmap_free(struct ast_sdp_rtpmap *rtpmap)
{
	ast_free(rtpmap);
}

struct ast_sdp_rtpmap *ast_sdp_a_get_rtpmap(const struct ast_sdp_a_line *a_line)
{
	char *value_copy;
	char *slash;
	int payload;
	char encoding_name[64];
	int clock_rate;
	char *encoding_parameters;
	struct ast_sdp_rtpmap *rtpmap;
	int clock_rate_len;

	value_copy = ast_strip(ast_strdupa(a_line->value));

	if (sscanf(value_copy, "%30d %63s", &payload, encoding_name) != 2) {
		return NULL;
	}

	slash = strchr(encoding_name, '/');
	if (!slash) {
		return NULL;
	}
	*slash++ = '\0';
	if (ast_strlen_zero(encoding_name)) {
		return NULL;
	}
	if (sscanf(slash, "%30d%n", &clock_rate, &clock_rate_len) < 1) {
		return NULL;
	}

	slash += clock_rate_len;
	if (!ast_strlen_zero(slash)) {
		if (*slash == '/') {
			*slash++ = '\0';
			encoding_parameters = slash;
			if (ast_strlen_zero(encoding_parameters)) {
				return NULL;
			}
		} else {
			return NULL;
		}
	} else {
		encoding_parameters = "";
	}

	rtpmap = ast_sdp_rtpmap_alloc(payload, encoding_name, clock_rate,
		encoding_parameters);

	return rtpmap;
}

/*!
 * \brief Turn an SDP attribute into an sdp_rtpmap structure
 *
 * \param m_line The media section where this attribute was found.
 * \param payload The RTP payload to find an rtpmap for
 * \param[out] rtpmap The rtpmap to fill in.
 * \return Zero if successful, otherwise less than zero
 */
static struct ast_sdp_rtpmap *sdp_payload_get_rtpmap(const struct ast_sdp_m_line *m_line, int payload)
{
	struct ast_sdp_a_line *rtpmap_attr;

	rtpmap_attr = ast_sdp_m_find_attribute(m_line, "rtpmap", payload);
	if (!rtpmap_attr) {
		return NULL;
	}

	return ast_sdp_a_get_rtpmap(rtpmap_attr);
}

static void process_fmtp_value(const char *value, int payload, struct ast_rtp_codecs *codecs)
{
	char *param;
	char *param_start;
	char *param_end;
	size_t len;
	struct ast_format *replace;
	struct ast_format *format;

	/*
	 * Extract the "a=fmtp:%d %s" attribute parameter string value which
	 * starts after the colon.
	 */
	param_start = ast_skip_nonblanks(value);/* Skip payload type */
	param_start = ast_skip_blanks(param_start);
	param_end = ast_skip_nonblanks(param_start);
	if (param_end == param_start) {
		/* There is no parameter string */
		return;
	}
	len = param_end - param_start;
	param = ast_alloca(len + 1);
	memcpy(param, param_start, len);
	param[len] = '\0';

	format = ast_rtp_codecs_get_payload_format(codecs, payload);
	if (!format) {
		return;
	}

	replace = ast_format_parse_sdp_fmtp(format, param);
	if (replace) {
		ast_rtp_codecs_payload_replace_format(codecs, payload, replace);
		ao2_ref(replace, -1);
	}
	ao2_ref(format, -1);
}

/*!
 * \brief Find and process all fmtp attribute lines for a given payload
 *
 * \param m_line The stream on which to search for the fmtp attributes
 * \param payload The specific fmtp attribute to search for
 * \param codecs The current RTP codecs that have been built up
 */
static void process_fmtp_lines(const struct ast_sdp_m_line *m_line, int payload,
	struct ast_rtp_codecs *codecs)
{
	const struct ast_sdp_a_line *a_line;
	int idx;

	idx = ast_sdp_m_find_a_first(m_line, "fmtp", payload);
	for (; 0 <= idx; idx = ast_sdp_m_find_a_next(m_line, idx, "fmtp", payload)) {
		a_line = ast_sdp_m_get_a(m_line, idx);
		ast_assert(a_line != NULL);

		process_fmtp_value(a_line->value, payload, codecs);
	}
}

/*
 * Needed so we don't have an external function referenced as data.
 * The dynamic linker doesn't handle that very well.
 */
static void rtp_codecs_free(struct ast_rtp_codecs *codecs)
{
	if (codecs) {
		ast_rtp_codecs_payloads_destroy(codecs);
	}
}

/*!
 * \brief Convert an SDP stream into an Asterisk stream
 *
 * Given an m-line from an SDP, convert it into an ast_stream structure.
 * This takes formats, as well as clock-rate and fmtp attributes into account.
 *
 * \param m_line The SDP media section to convert
 * \param g726_non_standard Non-zero if G.726 is non-standard
 *
 * \retval NULL An error occurred
 * \retval non-NULL The converted stream
 */
static struct ast_stream *get_stream_from_m(const struct ast_sdp_m_line *m_line, int g726_non_standard)
{
	int i;
	int non_ast_fmts;
	struct ast_rtp_codecs *codecs;
	struct ast_format_cap *caps;
	struct ast_stream *stream;
	enum ast_rtp_options options;

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		return NULL;
	}
	stream = ast_stream_alloc(m_line->type, ast_media_type_from_str(m_line->type));
	if (!stream) {
		ao2_ref(caps, -1);
		return NULL;
	}

	switch (ast_stream_get_type(stream)) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
		codecs = ast_calloc(1, sizeof(*codecs));
		if (!codecs || ast_rtp_codecs_payloads_initialize(codecs)) {
			rtp_codecs_free(codecs);
			ast_stream_free(stream);
			ao2_ref(caps, -1);
			ast_free(codecs);
			return NULL;
		}

		options = g726_non_standard ? AST_RTP_OPT_G726_NONSTANDARD : 0;
		for (i = 0; i < ast_sdp_m_get_payload_count(m_line); ++i) {
			struct ast_sdp_payload *payload_s;
			struct ast_sdp_rtpmap *rtpmap;
			int payload;

			payload_s = ast_sdp_m_get_payload(m_line, i);
			sscanf(payload_s->fmt, "%30d", &payload);

			rtpmap = sdp_payload_get_rtpmap(m_line, payload);
			if (!rtpmap) {
				/* No rtpmap attribute.  Try static payload type format assignment */
				ast_rtp_codecs_payloads_set_m_type(codecs, NULL, payload);
				continue;
			}

			if (!ast_rtp_codecs_payloads_set_rtpmap_type_rate(codecs, NULL, payload,
				m_line->type, rtpmap->encoding_name, options, rtpmap->clock_rate)) {
				/* Successfully mapped the payload type to format */
				process_fmtp_lines(m_line, payload, codecs);
			}
			ast_sdp_rtpmap_free(rtpmap);
		}

		ast_rtp_codecs_payload_formats(codecs, caps, &non_ast_fmts);
		ast_stream_set_data(stream, AST_STREAM_DATA_RTP_CODECS, codecs,
			(ast_stream_data_free_fn) rtp_codecs_free);
		break;
	case AST_MEDIA_TYPE_IMAGE:
		for (i = 0; i < ast_sdp_m_get_payload_count(m_line); ++i) {
			struct ast_sdp_payload *payload;

			/* As we don't carry T.38 over RTP we do our own format check */
			payload = ast_sdp_m_get_payload(m_line, i);
			if (!strcasecmp(payload->fmt, "t38")) {
				ast_format_cap_append(caps, ast_format_t38, 0);
			}
		}
		break;
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_TEXT:
	case AST_MEDIA_TYPE_END:
		break;
	}

	ast_stream_set_formats(stream, caps);
	ao2_ref(caps, -1);

	return stream;
}

struct ast_stream_topology *ast_get_topology_from_sdp(const struct ast_sdp *sdp, int g726_non_standard)
{
	struct ast_stream_topology *topology;
	int i;

	topology = ast_stream_topology_alloc();
	if (!topology) {
		return NULL;
	}

	for (i = 0; i < ast_sdp_get_m_count(sdp); ++i) {
		struct ast_stream *stream;

		stream = get_stream_from_m(ast_sdp_get_m(sdp, i), g726_non_standard);
		if (!stream) {
			/*
			 * The topology cannot match the SDP because
			 * we failed to create a corresponding stream.
			 */
			ast_stream_topology_free(topology);
			return NULL;
		}
		if (ast_stream_topology_append_stream(topology, stream) < 0) {
			/* Failed to add stream to topology */
			ast_stream_free(stream);
			ast_stream_topology_free(topology);
			return NULL;
		}
	}

	return topology;
}
