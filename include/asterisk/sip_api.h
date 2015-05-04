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

#ifndef __ASTERISK_SIP_H
#define __ASTERISK_SIP_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/config.h"

#define AST_SIP_API_VERSION 1

struct ast_sip_api_tech {
	const int version;
	const char *name;
	int (*sipinfo_send)(struct ast_channel *chan,
			struct ast_variable *headers, const char *content_type,
			const char *content, const char *useragent_filter);
};

/*!
 * \brief Send a customized SIP INFO request
 *
 * \param chan Channel
 * \param headers The headers to add to the INFO request
 * \param content_type The content type header to add
 * \param content The body of the INFO request
 * \param useragent_filter If non-NULL, only send the INFO if the
 * recipient's User-Agent contains useragent_filter as a substring
 *
 * \retval 0 Success
 * \retval non-zero Failure
 */
int ast_sipinfo_send(struct ast_channel *chan,
		struct ast_variable *headers,
		const char *content_type,
		const char *content,
		const char *useragent_filter);

/*!
 * \brief Register a SIP API provider
 *
 * This will fail if a provider has already registered or if the
 * provider is using an incorrect version.
 *
 * \param provider The provider to register
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sip_api_provider_register(const struct ast_sip_api_tech *provider);

/*!
 * \brief Unregister a SIP API provider
 */
void ast_sip_api_provider_unregister(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* __ASTERISK_SIP_H */
