/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sean Bright
 *
 * Sean Bright <sean.bright@gmail.com>
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
 * \brief DNS TXT Record Parsing API
 * \author Sean Bright <sean.bright@gmail.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <netinet/in.h>
#include <resolv.h>

#include "asterisk/dns_core.h"
#include "asterisk/dns_txt.h"
#include "asterisk/dns_internal.h"
#include "asterisk/utils.h"

struct ast_dns_record *dns_txt_alloc(struct ast_dns_query *query, const char *data, const size_t size)
{
	struct ast_dns_txt_record *txt;
	const char *end_of_record = data + size;
	size_t count = 0;

	/* Because we can't allocate additional memory, the best we can do here is just
	 * validate that this conforms to a TXT record. */
	while (data < end_of_record) {
		uint8_t byte_count = (uint8_t) *data;
		count++;
		data += byte_count + 1;
	}

	if (data != end_of_record) {
		/* This is not a valid TXT record, so we can bail out */
		return NULL;
	}

	txt = ast_calloc(1, sizeof(*txt) + size);
	if (!txt) {
		return NULL;
	}

	txt->count = count;
	txt->generic.data_ptr = txt->data;

	return (struct ast_dns_record *) txt;
}

size_t ast_dns_txt_get_count(const struct ast_dns_record *record)
{
	struct ast_dns_txt_record *txt = (struct ast_dns_txt_record *) record;
	ast_assert(ast_dns_record_get_rr_type(record) == T_TXT);
	return txt->count;
}

struct ast_vector_string *ast_dns_txt_get_strings(const struct ast_dns_record *record)
{
	struct ast_vector_string *strings;

	const size_t size = ast_dns_record_get_data_size(record);
	const char *data = ast_dns_record_get_data(record);
	const char *end_of_record = data + size;

	ast_assert(ast_dns_record_get_rr_type(record) == T_TXT);

	strings = ast_malloc(sizeof(struct ast_vector_const_string));
	if (!strings) {
		return NULL;
	}

	if (AST_VECTOR_INIT(strings, ast_dns_txt_get_count(record))) {
		ast_free(strings);
		return NULL;
	}

	while (data < end_of_record) {
		char *s;
		uint8_t bytes = (uint8_t) *data;

		s = ast_malloc(bytes + 1);
		if (!s) {
			ast_dns_txt_free_strings(strings);
			return NULL;
		}

		memcpy(s, &data[1], bytes);
		s[bytes] = 0;

		/* We know the size in advance so this can't fail */
		AST_VECTOR_APPEND(strings, s);

		data += bytes + 1;
	}

	/* Sanity check */
	if (data != end_of_record) {
		ast_dns_txt_free_strings(strings);
		return NULL;
	}

	return strings;
}

void ast_dns_txt_free_strings(struct ast_vector_string *strings)
{
	AST_VECTOR_CALLBACK_VOID(strings, ast_free);
	AST_VECTOR_PTR_FREE(strings);
}
