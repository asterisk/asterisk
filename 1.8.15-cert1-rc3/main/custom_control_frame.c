/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * David Vossel <dvosse@digium.com>
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
 * \brief Encode and Decode custom control frame payload types.
 *
 * \author David Vossel <dvossel@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"

#include "asterisk/custom_control_frame.h"

struct ast_custom_payload {
	enum ast_custom_payload_type type;
	/*! length of data portion only */
	size_t datalen;
	char *data;
};

enum ast_custom_payload_type ast_custom_payload_type(struct ast_custom_payload *type)
{
	return type->type;
}

size_t ast_custom_payload_len(struct ast_custom_payload *type)
{
	return type->datalen + sizeof(struct ast_custom_payload);
}

struct custom_sipinfo {
	size_t num_headers;
	int content_present;
	int useragent_filter_present;
	char *data;
};

struct ast_custom_payload *ast_custom_payload_sipinfo_encode(struct ast_variable *headers,
	const char *content_type,
	const char *content,
	const char *useragent_filter)
{
	int num_headers = 0;
	int content_present = 0;
	int content_strlen = 0;
	int content_type_strlen = 0;
	int useragent_filter_present = 0;
	int useragent_filter_len = 0;
	size_t datalen = 0;
	struct ast_variable *var;
	struct ast_custom_payload *payload;
	struct custom_sipinfo *sipinfo;
	char *data;

	datalen += sizeof(struct custom_sipinfo);

	for (var = headers; var; var = var->next) {
		datalen += strlen(var->name) + 1;
		datalen += strlen(var->value) + 1;
		num_headers++;
	}

	if (!ast_strlen_zero(content_type) && !ast_strlen_zero(content)) {
		content_type_strlen = strlen(content_type);
		content_strlen = strlen(content);
		datalen += content_type_strlen + 1;
		datalen += content_strlen + 1;
		content_present = 1;
	}

	if (!ast_strlen_zero(useragent_filter)) {
		useragent_filter_len = strlen(useragent_filter);
		datalen += useragent_filter_len + 1;
		useragent_filter_present = 1;
	}

	if (!(payload = ast_calloc(1, datalen + sizeof(*payload)))) {
		return NULL;
	}

	payload->type = AST_CUSTOM_SIP_INFO;
	payload->datalen = datalen;
	payload->data = (char *) payload + sizeof(struct ast_custom_payload);
	sipinfo = (struct custom_sipinfo *) payload->data;
	sipinfo->num_headers = num_headers;
	sipinfo->content_present = content_present;
	sipinfo->useragent_filter_present = useragent_filter_present;
	sipinfo->data = (char *) sipinfo + sizeof(struct custom_sipinfo);

	/* store string buffers in payload data
	 * headers are put in first, followed by content type and then content body. */
	data = sipinfo->data;

	for (var = headers; var; var = var->next) {
		int namelen = strlen(var->name);
		int vallen = strlen(var->value);

		/*! we already know we have enough room for each of these */
		ast_copy_string(data, var->name, namelen+1);
		data += namelen + 1; /* skip over the '\0' character */
		ast_copy_string(data, var->value, vallen+1);
		data += vallen + 1; /* skip over the '\0' character */
	}

	if (content_present) {
		ast_copy_string(data, content_type, content_type_strlen+1);
		data += content_type_strlen + 1;
		ast_copy_string(data, content, content_strlen+1);
		data += content_strlen + 1;
	}

	if (useragent_filter_present) {
		ast_copy_string(data, useragent_filter, useragent_filter_len+1);
	}

	return payload;
}

int ast_custom_payload_sipinfo_decode(struct ast_custom_payload *pl,
	struct ast_variable **headers,
	char **content_type,
	char **content,
	char **useragent_filter)
{
	struct custom_sipinfo *sipinfo;
	struct ast_variable *cur = NULL;
	char *data;
	int i;

	*headers = NULL;
	*content_type = NULL;
	*content = NULL;
	*useragent_filter = NULL;

	if (pl->type != AST_CUSTOM_SIP_INFO) {
		return -1;
	}

	sipinfo = (struct custom_sipinfo *) pl->data;
	data = sipinfo->data;
	for (i = 0; i < sipinfo->num_headers; i++) {
		const char *name;
		const char *value;

		name = data;
		data += strlen(name) + 1;
		value = data;
		data += strlen(value) + 1;

		if (*headers) {
			if ((cur->next = ast_variable_new(name, value, ""))) {
				cur = cur->next;
			}
		} else {
			*headers = cur = ast_variable_new(name, value, "");
		}
	}

	if (sipinfo->content_present) {
		*content_type = ast_strdup(data);
		data += strlen(data) + 1;
		*content = ast_strdup(data);
		data += strlen(data) + 1;
	}

	if (sipinfo->useragent_filter_present) {
		*useragent_filter = ast_strdup(data);
	}
	return 0;
}
