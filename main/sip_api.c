/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
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

#include "asterisk/sip_api.h"
#include "asterisk/logger.h"

static const struct ast_sip_api_tech *api_provider;

int ast_sipinfo_send(struct ast_channel *chan,
		struct ast_variable *headers,
		const char *content_type,
		const char *content,
		const char *useragent_filter)
{
	if (!api_provider) {
		ast_log(LOG_WARNING, "Unable to send custom SIP INFO. No API provider registered\n");
		return -1;
	}

	return api_provider->sipinfo_send(chan, headers, content_type, content, useragent_filter);
}

int ast_sip_api_provider_register(const struct ast_sip_api_tech *provider)
{
	if (api_provider) {
		ast_log(LOG_WARNING, "SIP provider %s has already registered. Not registering provider %s\n",
				api_provider->name, provider->name);
		return -1;
	}

	if (provider->version != AST_SIP_API_VERSION) {
		ast_log(LOG_WARNING, "SIP API provider version mismatch: Current version is %d but provider "
				"uses version %d\n", AST_SIP_API_VERSION, provider->version);
		return -1;
	}

	api_provider = provider;
	return 0;
}

void ast_sip_api_provider_unregister(void)
{
	api_provider = NULL;
}
