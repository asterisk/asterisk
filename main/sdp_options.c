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

struct ast_sdp_options {
	enum ast_sdp_options_ice ice;
	int telephone_event;
	enum ast_sdp_options_repr repr;
	enum ast_sdp_options_encryption encryption;
};

#define DEFAULT_ICE AST_SDP_ICE_DISABLED
#define DEFAULT_TELEPHONE_EVENT 0
#define DEFAULT_REPR AST_SDP_REPR_STRING
#define DEFAULT_ENCRYPTION AST_SDP_ENCRYPTION_DISABLED

static void set_defaults(struct ast_sdp_options *options)
{
	options->ice = DEFAULT_ICE;
	options->telephone_event = DEFAULT_TELEPHONE_EVENT;
	options->repr = DEFAULT_REPR;
	options->encryption = DEFAULT_ENCRYPTION;
}

struct ast_sdp_options *ast_sdp_options_alloc(void)
{
	struct ast_sdp_options *options;

	options = ast_calloc(1, sizeof(*options));
	if (!options) {
		return NULL;
	}
	set_defaults(options);
	return options;
}

void ast_sdp_options_free(struct ast_sdp_options *options)
{
	ast_free(options);
}

int ast_sdp_options_set_ice(struct ast_sdp_options *options, enum ast_sdp_options_ice ice_setting)
{
	ast_assert(options != NULL);

	options->ice = ice_setting;
	return 0;
}

enum ast_sdp_options_ice ast_sdp_options_get_ice(const struct ast_sdp_options *options)
{
	ast_assert(options != NULL);

	return options->ice;
}

int ast_sdp_options_set_telephone_event(struct ast_sdp_options *options, int telephone_event_enabled)
{
	ast_assert(options != NULL);

	options->telephone_event = telephone_event_enabled;
	return 0;
}

int ast_sdp_options_get_telephone_event(const struct ast_sdp_options *options)
{
	ast_assert(options != NULL);

	return options->telephone_event;
}

int ast_sdp_options_set_repr(struct ast_sdp_options *options, enum ast_sdp_options_repr repr)
{
	ast_assert(options != NULL);

	options->repr = repr;
	return 0;
}

enum ast_sdp_options_repr ast_sdp_options_get_repr(const struct ast_sdp_options *options)
{
	ast_assert(options != NULL);

	return options->repr;
}

int ast_sdp_options_set_encryption(struct ast_sdp_options *options,
	enum ast_sdp_options_encryption encryption)
{
	ast_assert(options != NULL);

	options->encryption = encryption;
	return 0;
}

enum ast_sdp_options_encryption ast_sdp_options_get_encryption(const struct ast_sdp_options *options)
{
	ast_assert(options != NULL);

	return options->encryption;
}
