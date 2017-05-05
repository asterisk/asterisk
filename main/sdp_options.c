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
DEFINE_STRINGFIELD_GETTERS_SETTERS_FOR(sdpowner, 0);
DEFINE_STRINGFIELD_GETTERS_SETTERS_FOR(sdpsession, 0);
DEFINE_STRINGFIELD_GETTERS_SETTERS_FOR(rtp_engine, 0);

DEFINE_GETTERS_SETTERS_FOR(unsigned int, bind_rtp_to_media_address);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, bind_udptl_to_media_address);
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
DEFINE_GETTERS_SETTERS_FOR(enum ast_sdp_options_dtmf, dtmf);
DEFINE_GETTERS_SETTERS_FOR(enum ast_sdp_options_ice, ice);
DEFINE_GETTERS_SETTERS_FOR(enum ast_sdp_options_impl, impl);
DEFINE_GETTERS_SETTERS_FOR(enum ast_sdp_options_encryption, encryption);
DEFINE_GETTERS_SETTERS_FOR(unsigned int, ssrc);

static void set_defaults(struct ast_sdp_options *options)
{
	options->dtmf = DEFAULT_DTMF;
	options->ice = DEFAULT_ICE;
	options->impl = DEFAULT_IMPL;
	options->encryption = DEFAULT_ENCRYPTION;
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
	ast_string_field_free_memory(options);
	ast_free(options);
}
