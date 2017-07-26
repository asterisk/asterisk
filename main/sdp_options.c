/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
#include "asterisk/sdp_options.h"

#include "sdp_private.h"

#define DEFAULT_DTMF AST_SDP_DTMF_NONE
#define DEFAULT_ICE AST_SDP_ICE_DISABLED
#define DEFAULT_IMPL AST_SDP_IMPL_STRING
#define DEFAULT_ENCRYPTION AST_SDP_ENCRYPTION_DISABLED
#define DEFAULT_MAX_STREAMS 16	/* Set to match our PJPROJECT PJMEDIA_MAX_SDP_MEDIA. */

#define DEFINE_STRINGFIELD_GETTERS_SETTERS_FOR(field, assert_on_null) \
void ast_sdp_options_set_##field(struct ast_sdp_options *options, const char *value) \
{ \
	ast_assert(options != NULL); \
	if ((assert_on_null)) ast_assert(!ast_strlen_zero(value)); \
	if (!strcmp(value, options->field)) return; \
	ast_string_field_set(options, field, value); \
} \
const char *ast_sdp_options_get_##field(const struct ast_sdp_options *options) \
{ \
	ast_assert(options != NULL); \
	return options->field; \
} \

#define DEFINE_GETTERS_SETTERS_FOR(type, field) \
void ast_sdp_options_set_##field(struct ast_sdp_options *options, type value) \
{ \
	ast_assert(options != NULL); \
	options->field = value; \
} \
type ast_sdp_options_get_##field(const struct ast_sdp_options *options) \
{ \
	ast_assert(options != NULL); \
	return options->field; \
} \

DEFINE_STRINGFIELD_GETTERS_SETTERS_FOR(media_address, 0);
DEFINE_STRINGFIELD_GETTERS_SETTERS_FOR(interface_address, 0);
DEFINE_STRINGFIELD_GETTERS_SETTERS_FOR(sdpowner, 0);
DEFINE_STRINGFIELD_GETTERS_SETTERS_FOR(sdpsession, 0);
DEFINE_STRINGFIELD_GETTERS_SETTERS_FOR(rtp_engine, 0);

DEFINE_GETTERS_SETTERS_FOR(void *, state_context);
DEFINE_GETTERS_SETTERS_FOR(ast_sdp_answerer_modify_cb, answerer_modify_cb);
DEFINE_GETTERS_SETTERS_FOR(ast_sdp_offerer_modify_cb, offerer_modify_cb);
DEFINE_GETTERS_SETTERS_FOR(ast_sdp_offerer_config_cb, offerer_config_cb);
DEFINE_GETTERS_SETTERS_FOR(ast_sdp_preapply_cb, preapply_cb);
DEFINE_GETTERS_SETTERS_FOR(ast_sdp_postapply_cb, postapply_cb);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, rtp_symmetric);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, udptl_symmetric);
DEFINE_GETTERS_SETTERS_FOR(enum ast_t38_ec_modes, udptl_error_correction);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, udptl_far_max_datagram);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, rtp_ipv6);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, g726_non_standard);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, rtcp_mux);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, tos_audio);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, cos_audio);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, tos_video);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, cos_video);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, max_streams);
DEFINE_GETTERS_SETTERS_FOR(enum ast_sdp_options_dtmf, dtmf);
DEFINE_GETTERS_SETTERS_FOR(enum ast_sdp_options_ice, ice);
DEFINE_GETTERS_SETTERS_FOR(enum ast_sdp_options_impl, impl);
DEFINE_GETTERS_SETTERS_FOR(enum ast_sdp_options_encryption, encryption);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, ssrc);

struct ast_sched_context *ast_sdp_options_get_sched_type(const struct ast_sdp_options *options, enum ast_media_type type)
{
	struct ast_sched_context *sched = NULL;

	switch (type) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
	case AST_MEDIA_TYPE_IMAGE:
	case AST_MEDIA_TYPE_TEXT:
		sched = options->sched[type];
		break;
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_END:
		break;
	}
	return sched;
}

void ast_sdp_options_set_sched_type(struct ast_sdp_options *options, enum ast_media_type type, struct ast_sched_context *sched)
{
	switch (type) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
	case AST_MEDIA_TYPE_IMAGE:
	case AST_MEDIA_TYPE_TEXT:
		options->sched[type] = sched;
		break;
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_END:
		break;
	}
}

struct ast_format_cap *ast_sdp_options_get_format_cap_type(const struct ast_sdp_options *options,
	enum ast_media_type type)
{
	struct ast_format_cap *cap = NULL;

	switch (type) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
	case AST_MEDIA_TYPE_IMAGE:
	case AST_MEDIA_TYPE_TEXT:
		cap = options->caps[type];
		break;
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_END:
		break;
	}
	return cap;
}

void ast_sdp_options_set_format_cap_type(struct ast_sdp_options *options,
	enum ast_media_type type, struct ast_format_cap *cap)
{
	switch (type) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
	case AST_MEDIA_TYPE_IMAGE:
	case AST_MEDIA_TYPE_TEXT:
		ao2_cleanup(options->caps[type]);
		options->caps[type] = NULL;
		if (cap && !ast_format_cap_empty(cap)) {
			ao2_ref(cap, +1);
			options->caps[type] = cap;
		}
		break;
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_END:
		break;
	}
}

void ast_sdp_options_set_format_caps(struct ast_sdp_options *options,
	struct ast_format_cap *cap)
{
	enum ast_media_type type;

	for (type = AST_MEDIA_TYPE_UNKNOWN; type < AST_MEDIA_TYPE_END; ++type) {
		ao2_cleanup(options->caps[type]);
		options->caps[type] = NULL;
	}

	if (!cap || ast_format_cap_empty(cap)) {
		return;
	}

	for (type = AST_MEDIA_TYPE_UNKNOWN + 1; type < AST_MEDIA_TYPE_END; ++type) {
		struct ast_format_cap *type_cap;

		type_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (!type_cap) {
			continue;
		}

		ast_format_cap_set_framing(type_cap, ast_format_cap_get_framing(cap));
		if (ast_format_cap_append_from_cap(type_cap, cap, type)
			|| ast_format_cap_empty(type_cap)) {
			ao2_ref(type_cap, -1);
			continue;
		}

		/* This takes the allocation reference */
		options->caps[type] = type_cap;
	}
}

static void set_defaults(struct ast_sdp_options *options)
{
	options->dtmf = DEFAULT_DTMF;
	options->ice = DEFAULT_ICE;
	options->impl = DEFAULT_IMPL;
	options->encryption = DEFAULT_ENCRYPTION;
	options->max_streams = DEFAULT_MAX_STREAMS;
}

struct ast_sdp_options *ast_sdp_options_alloc(void)
{
	struct ast_sdp_options *options;

	options = ast_calloc(1, sizeof(*options));
	if (!options) {
		return NULL;
	}

	if (ast_string_field_init(options, 256)) {
		ast_free(options);
		return NULL;
	}

	set_defaults(options);
	return options;
}

void ast_sdp_options_free(struct ast_sdp_options *options)
{
	enum ast_media_type type;

	if (!options) {
		return;
	}

	for (type = AST_MEDIA_TYPE_UNKNOWN; type < AST_MEDIA_TYPE_END; ++type) {
		ao2_cleanup(options->caps[type]);
	}
	ast_string_field_free_memory(options);
	ast_free(options);
}
